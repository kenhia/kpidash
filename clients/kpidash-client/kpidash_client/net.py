"""
net.py — per-host network health for the panel's network card (WI #4061)

Publishes `kdash:net:{host}`, contract v1 in kdashdata
(contracts/schemas/kdash-net.schema.json, CD-27). Linux only: the inputs are
/proc/net/bonding/<bond> and /sys/class/net, and cleo's Windows producer lives
in kpidash-client-win.

Each host probes its OWN path to the UDM, its fabric peers and the Internet.
That is what makes the fabric-uplink failure visible (peers answer, the UDM
does not), and the reason the probes are not centralised.

Three fields need memory across restarts — `lfc_delta_24h`,
`rx_errors_delta_24h` and `drops` — so the collector keeps a small on-disk
history under the client's state dir (NetHistory). A restart therefore keeps
the last 24 h of counter samples and the last hour of drops, and never
reports `+0` for a window it did not see: `(+0)` on the card means "no
flaps", not "the client just restarted".
"""

from __future__ import annotations

import json
import logging
import os
import re
import socket
import subprocess
import time
from dataclasses import dataclass, field
from pathlib import Path

logger = logging.getLogger(__name__)

SYS_NET = Path("/sys/class/net")
PROC_BONDING = Path("/proc/net/bonding")

#: The fabric as of 2026-10-26 (klams 01a17a66): each server's bond0 address.
#: A host never probes itself; the list is overridable as `[net] peers`.
DEFAULT_PEERS = ("192.168.1.235", "192.168.1.60", "192.168.1.111", "192.168.1.200")
DEFAULT_UDM = "192.168.1.1"
DEFAULT_WAN = "1.1.1.1"

#: Display labels for the fabric NICs on the fleet, by PCI vendor:device.
#: Display only (the schema's optional `nic`); `[net] nic` overrides.
NIC_LABELS = {
    ("0x1d6a", "0x04c0"): "AQC113",
    ("0x1d6a", "0xd107"): "AQC107",
    ("0x1d6a", "0x07b1"): "AQC107",
    ("0x8086", "0x1563"): "X550",
    ("0x10ec", "0x8126"): "RTL8126",
}

DAY_S = 86400.0
HOUR_S = 3600.0
DROPS_MAX = 64  # schema maxItems
#: One kept counter sample per this many seconds — 288 a day, so the history
#: file stays a few KB and is rewritten about every five minutes, not every 5 s
#: (rpi53 runs off an SD card).
SAMPLE_KEEP_S = 300.0
PROBE_TIMEOUT_S = 1.0
PROBE_DEADLINE_S = 2.5


# ---------------------------------------------------------------------------
# /proc/net/bonding
# ---------------------------------------------------------------------------


@dataclass
class Slave:
    name: str
    mii_up: bool = False
    speed_mbps: int | None = None
    lfc: int | None = None


@dataclass
class Bond:
    primary: str | None = None
    active: str | None = None
    mii_up: bool = False
    slaves: dict[str, Slave] = field(default_factory=dict)


_SPEED_RE = re.compile(r"^(\d+)\s*Mbps$")


def parse_bonding(text: str) -> Bond:
    """Parse /proc/net/bonding/<bond>. Unknown lines are ignored."""
    bond = Bond()
    cur: Slave | None = None
    for raw in text.splitlines():
        if ":" not in raw:
            continue
        key, _, val = raw.partition(":")
        key, val = key.strip(), val.strip()
        if key == "Slave Interface":
            cur = Slave(name=val)
            bond.slaves[val] = cur
        elif cur is None:
            if key == "Primary Slave":
                # "enp12s0 (primary_reselect always)" or "None"
                name = val.split()[0] if val else "None"
                bond.primary = None if name == "None" else name
            elif key == "Currently Active Slave":
                bond.active = None if val in ("", "None") else val
            elif key == "MII Status":
                bond.mii_up = val == "up"
        elif key == "MII Status":
            cur.mii_up = val == "up"
        elif key == "Speed":
            m = _SPEED_RE.match(val)
            cur.speed_mbps = int(m.group(1)) if m else None
        elif key == "Link Failure Count":
            try:
                cur.lfc = int(val)
            except ValueError:
                cur.lfc = None
    return bond


def classify_leg(speed_mbps: int | None, link_up: bool) -> str:
    """The contract's `leg` (CD-27): the producer's classification of the speed."""
    if not link_up or speed_mbps is None or speed_mbps <= 0:
        return "down"
    if speed_mbps >= 10000:
        return "10g"
    if speed_mbps >= 5000:
        return "5g"
    return "1g"


# ---------------------------------------------------------------------------
# sysfs
# ---------------------------------------------------------------------------


def _read_int(path: Path) -> int | None:
    try:
        return int(path.read_text().strip())
    except (OSError, ValueError):
        return None


def _read_str(path: Path) -> str | None:
    try:
        return path.read_text().strip()
    except OSError:
        return None


def nic_label(iface: str, sys_net: Path = SYS_NET) -> str | None:
    dev = sys_net / iface / "device"
    vendor, device = _read_str(dev / "vendor"), _read_str(dev / "device")
    if vendor is None or device is None:
        return None
    return NIC_LABELS.get((vendor.lower(), device.lower()))


def default_route_iface(proc_route: Path = Path("/proc/net/route")) -> str | None:
    """The interface carrying the IPv4 default route — a no-bond host's fabric NIC."""
    try:
        lines = proc_route.read_text().splitlines()[1:]
    except OSError:
        return None
    for line in lines:
        cols = line.split()
        if len(cols) > 2 and cols[1] == "00000000":
            return cols[0]
    return None


def local_ipv4s() -> set[str]:
    try:
        import psutil

        return {
            a.address
            for addrs in psutil.net_if_addrs().values()
            for a in addrs
            if a.family == socket.AF_INET
        }
    except Exception:
        return set()


# ---------------------------------------------------------------------------
# Probes
# ---------------------------------------------------------------------------

_RTT_RE = re.compile(r"time[=<]([\d.]+)\s*ms")


def parse_ping_rtt(output: str) -> float | None:
    m = _RTT_RE.search(output)
    return float(m.group(1)) if m else None


def probe_all(targets: list[str], deadline_s: float = PROBE_DEADLINE_S) -> dict[str, float | None]:
    """One ICMP echo to every target, concurrently, bounded by `deadline_s`.

    Returns target -> RTT in ms, or None when it did not answer. Uses the
    system `ping` (which carries cap_net_raw) because the fleet's
    net.ipv4.ping_group_range leaves unprivileged ICMP sockets off. A dead
    target costs at most the deadline, and every straggler is killed, so a
    probe can never stall the loop.
    """
    procs: dict[str, subprocess.Popen] = {}
    for t in dict.fromkeys(targets):
        try:
            procs[t] = subprocess.Popen(
                ["ping", "-n", "-c", "1", "-W", str(int(PROBE_TIMEOUT_S)), t],
                stdout=subprocess.PIPE,
                stderr=subprocess.DEVNULL,
                text=True,
            )
        except OSError as e:
            logger.debug("ping %s: %s", t, e)
    out: dict[str, float | None] = {t: None for t in targets}
    end = time.monotonic() + deadline_s
    for t, p in procs.items():
        try:
            stdout, _ = p.communicate(timeout=max(0.0, end - time.monotonic()))
            if p.returncode == 0:
                out[t] = parse_ping_rtt(stdout)
                if out[t] is None:  # answered, RTT unparseable: still reachable
                    out[t] = 0.0
        except subprocess.TimeoutExpired:
            p.kill()
            p.communicate()
    return out


# ---------------------------------------------------------------------------
# Producer memory
# ---------------------------------------------------------------------------


def _delta(samples: list[tuple[float, int]], now: float) -> int | None:
    """Rise of a cumulative counter over the trailing 24 h.

    The baseline is the oldest sample INSIDE the window — the rule slice 3.5's
    Windows producer follows too (korg:4066 comment 3995), so the two agree. A
    decrease is a counter reset (bond re-created, reboot) and is counted from
    the reset, never negative (CD-27). None with fewer than two samples: there
    is no window to measure, and 0 would claim one.
    """
    window = [(ts, v) for ts, v in samples if now - ts <= DAY_S]
    if len(window) < 2:
        return None
    total = 0
    for (_, a), (_, b) in zip(window, window[1:], strict=False):
        total += b - a if b >= a else b
    return total


class NetHistory:
    """Counter samples (24 h) and drop events (1 h), persisted as one JSON file.

    `observe()` appends the latest counters and returns the drop events the
    increment implies. The file is rewritten only when a sample is kept or a
    drop recorded — about every five minutes on a quiet host.

    The last LINK STATE (the drop counter a drop is diffed against) is
    deliberately NOT persisted: a drop is a change seen between two ticks of
    this process. Persisting it would log a phantom drop on every boot, when
    the bond comes up with its counter re-initialised (korg:4066 comment
    3995, the ruling both producers follow).
    """

    def __init__(self, path: Path | None) -> None:
        self._path = path
        self.lfc: list[tuple[float, int]] = []
        self.rx: list[tuple[float, int]] = []
        self.drops: list[float] = []
        self.last_drop_counter: int | None = None
        self.last_drop_ts: float | None = None
        self._load()

    def _load(self) -> None:
        if not self._path:
            return
        try:
            data = json.loads(self._path.read_text())
            self.lfc = [(float(t), int(v)) for t, v in data.get("lfc", [])]
            self.rx = [(float(t), int(v)) for t, v in data.get("rx", [])]
            self.drops = [float(t) for t in data.get("drops", [])]
        except FileNotFoundError:
            pass
        except (OSError, ValueError, TypeError, AttributeError) as e:
            logger.warning("net history %s unreadable (%s) — starting empty", self._path, e)

    def save(self) -> None:
        if not self._path:
            return
        data = {
            "lfc": self.lfc,
            "rx": self.rx,
            "drops": self.drops,
        }
        try:
            self._path.parent.mkdir(parents=True, exist_ok=True)
            tmp = self._path.with_suffix(".tmp")
            tmp.write_text(json.dumps(data))
            os.replace(tmp, self._path)
        except OSError as e:
            logger.warning("net history %s not saved: %s", self._path, e)

    @staticmethod
    def _keep(series: list[tuple[float, int]], now: float, value: int | None) -> bool:
        """Append (now, value) to a series; True when it became a KEPT sample."""
        if value is None:
            return False
        kept = False
        # The newest entry is provisional until SAMPLE_KEEP_S has passed since
        # the one before it; that keeps the ring at ~288 entries a day while
        # the newest value is always present. Overwriting a provisional value
        # with a larger one loses nothing (increments telescope); a DECREASE
        # is a reset, and is always kept so the delta can count from it.
        if (
            len(series) >= 2
            and series[-1][0] - series[-2][0] < SAMPLE_KEEP_S
            and value >= series[-1][1]
        ):
            series[-1] = (now, value)
        else:
            series.append((now, value))
            kept = True
        while series and now - series[0][0] > DAY_S + SAMPLE_KEEP_S:
            series.pop(0)
        return kept

    def observe(
        self, now: float, lfc: int | None, rx: int | None, drop_counter: int | None
    ) -> dict:
        dirty = self._keep(self.lfc, now, lfc) | self._keep(self.rx, now, rx)

        # Drops: increments of the link's failure counter (the primary slave's
        # LFC on a bond, carrier_down_count on a single NIC) between two ticks
        # of THIS process. The first tick after a start only sets the baseline.
        # An increment seen across a gap longer than an hour is not placed; a
        # decrease is a reset and counts from zero, like the deltas.
        if drop_counter is not None:
            prev, prev_ts = self.last_drop_counter, self.last_drop_ts
            if prev is not None and prev_ts is not None and now - prev_ts <= HOUR_S:
                n = drop_counter - prev if drop_counter >= prev else drop_counter
                for _ in range(min(n, DROPS_MAX)):
                    self.drops.append(now)
                if n:
                    dirty = True
            self.last_drop_counter, self.last_drop_ts = drop_counter, now
        before = len(self.drops)
        self.drops = [t for t in self.drops if now - t < HOUR_S][-DROPS_MAX:]
        dirty |= len(self.drops) != before
        if dirty:
            self.save()

        return {
            "lfc_delta_24h": _delta(self.lfc, now) if lfc is not None else None,
            "rx_errors_delta_24h": _delta(self.rx, now) if rx is not None else None,
            "drops": list(self.drops),
        }


def default_state_dir() -> Path:
    if os.environ.get("STATE_DIRECTORY"):
        return Path(os.environ["STATE_DIRECTORY"].split(":")[0])
    base = os.environ.get("XDG_STATE_HOME") or str(Path.home() / ".local" / "state")
    return Path(base) / "kpidash-client"


# ---------------------------------------------------------------------------
# Collector
# ---------------------------------------------------------------------------


@dataclass
class NetConfig:
    enabled: bool = True
    bond: str = "bond0"
    udm: str = DEFAULT_UDM
    wan: str = DEFAULT_WAN
    peers: list[str] = field(default_factory=lambda: list(DEFAULT_PEERS))
    nic: str | None = None
    state_dir: str | None = None


@dataclass
class LinkState:
    bond: bool
    fabric_if: str | None
    leg: str
    speed_mbps: int | None
    lfc: int | None
    drop_counter: int | None


def read_link(
    cfg: NetConfig, proc_bonding: Path = PROC_BONDING, sys_net: Path = SYS_NET
) -> LinkState:
    """Which leg carries traffic, and the fabric NIC whose counters we report."""
    try:
        text = (proc_bonding / cfg.bond).read_text()
    except OSError:
        text = None
    if text is not None:
        b = parse_bonding(text)
        # The fabric NIC is the PRIMARY slave (the 10G leg), whichever slave is
        # active; with no primary configured, the fastest slave stands in.
        fabric = b.primary
        if fabric is None and b.slaves:
            fabric = max(b.slaves.values(), key=lambda s: s.speed_mbps or 0).name
        active = b.slaves.get(b.active) if b.active else None
        up = bool(b.mii_up and active and active.mii_up)
        speed = active.speed_mbps if (up and active) else None
        leg = classify_leg(speed, up)
        prim = b.slaves.get(fabric) if fabric else None
        lfc = prim.lfc if prim else None
        return LinkState(True, fabric, leg, speed if leg != "down" else None, lfc, lfc)

    iface = default_route_iface()
    if iface is None:
        return LinkState(False, None, "down", None, None, None)
    carrier = _read_int(sys_net / iface / "carrier") == 1
    speed = _read_int(sys_net / iface / "speed")
    leg = classify_leg(speed, carrier)
    return LinkState(
        False,
        iface,
        leg,
        speed if leg != "down" else None,
        None,
        _read_int(sys_net / iface / "carrier_down_count"),
    )


class NetCollector:
    """Builds one `kdash:net:{host}` record per call to `collect()`."""

    def __init__(self, cfg: NetConfig, history: NetHistory | None = None) -> None:
        self._cfg = cfg
        if history is None:
            sd = Path(cfg.state_dir) if cfg.state_dir else default_state_dir()
            history = NetHistory(sd / "net-history.json")
        self._history = history
        mine = local_ipv4s()
        self._peers = [p for p in cfg.peers if p not in mine]
        self._last_bytes: tuple[float, int] | None = None

    def _bytes_per_s(self, iface: str | None, now: float) -> float:
        if not iface:
            return 0.0
        st = SYS_NET / iface / "statistics"
        rx, tx = _read_int(st / "rx_bytes"), _read_int(st / "tx_bytes")
        if rx is None or tx is None:
            self._last_bytes = None
            return 0.0
        total = rx + tx
        prev, self._last_bytes = self._last_bytes, (now, total)
        if prev is None or now <= prev[0] or total < prev[1]:
            return 0.0
        return (total - prev[1]) / (now - prev[0])

    def prime(self) -> None:
        """Take a first byte-counter reading so the first record has a rate."""
        link = read_link(self._cfg)
        self._bytes_per_s(link.fabric_if, time.time())

    def collect(self) -> dict:
        link = read_link(self._cfg)
        now = time.time()
        rx_errors = (
            _read_int(SYS_NET / link.fabric_if / "statistics" / "rx_errors")
            if link.fabric_if
            else None
        )
        rtts = probe_all([self._cfg.udm, self._cfg.wan, *self._peers])
        udm_rtt, wan_rtt = rtts.get(self._cfg.udm), rtts.get(self._cfg.wan)
        peers_ok = any(rtts.get(p) is not None for p in self._peers)
        mem = self._history.observe(now, link.lfc, rx_errors, link.drop_counter)

        rec: dict = {
            "leg": link.leg,
            "speed_mbps": link.speed_mbps,
            "bond": link.bond,
            "lfc": link.lfc if link.bond else None,
            "lfc_delta_24h": mem["lfc_delta_24h"] if link.bond else None,
            "rx_errors": rx_errors,
            "rx_errors_delta_24h": mem["rx_errors_delta_24h"],
            "reach": {"udm": udm_rtt is not None, "peers": peers_ok, "wan": wan_rtt is not None},
            "rtt_ms": {
                "udm": round(udm_rtt, 2) if udm_rtt is not None else None,
                "wan": round(wan_rtt, 2) if wan_rtt is not None else None,
            },
            "bytes_per_s": round(self._bytes_per_s(link.fabric_if, now), 1),
            "drops": mem["drops"],
            "ts": now,
        }
        label = self._cfg.nic or (nic_label(link.fabric_if) if link.fabric_if else None)
        if label:
            rec["nic"] = label[:16]
        return rec
