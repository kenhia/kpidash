"""tests/test_net.py — kdash:net:{host} producer (WI #4061)

The bonding fixtures are real /proc/net/bonding/bond0 captures, one per NIC
type on the fleet, taken 2026-10-26: AQC113 (kai, kubs0), X550 (kubsdb),
AQC107 (gratch).

Every record the collector builds is validated against the contract's own
schema with kdashdata's own validator — the submodule is part of the gate, so a
missing one fails here rather than skipping.
"""

from __future__ import annotations

import json
import sys
from pathlib import Path
from unittest.mock import MagicMock

import pytest

from kpidash_client import net
from kpidash_client.config import ClientConfig, ConfigError
from kpidash_client.net import (
    NetCollector,
    NetConfig,
    NetHistory,
    classify_leg,
    parse_bonding,
    parse_ping_rtt,
    read_link,
)
from kpidash_client.redis_client import RedisClient

FIXTURES = Path(__file__).parent / "fixtures" / "bonding"
KDASHDATA = Path(__file__).resolve().parents[3] / "lib" / "kdashdata"
SCHEMA = KDASHDATA / "contracts" / "schemas" / "kdash-net.schema.json"


@pytest.fixture(scope="module")
def validate():
    assert SCHEMA.exists(), (
        f"{SCHEMA} missing — run `git submodule update --init lib/kdashdata`"
    )
    sys.path.insert(0, str(KDASHDATA / "scripts"))
    try:
        import jsonschema_mini
    finally:
        sys.path.pop(0)
    schema = json.loads(SCHEMA.read_text())

    def _v(record):
        return jsonschema_mini.validate(record, schema)

    # The validator itself: every example passes, every counterexample fails.
    for ex in schema["examples"]:
        assert _v(ex) == []
    for cx in schema["x-counterexamples"]:
        assert _v(cx["record"]) != []
    return _v


# ---- /proc/net/bonding --------------------------------------------------------

#: fixture -> (primary, backup, primary LFC)
CAPTURES = {
    "kai-aqc113.txt": ("enp12s0", "enp11s0", 1),
    "kubs0-aqc113.txt": ("enp8s0", "enp7s0", 28),
    "kubsdb-x550.txt": ("enp48s0f0", "enp39s0", 2),
    "gratch-aqc107.txt": ("enp7s0", "enp5s0", 1),
}


@pytest.mark.parametrize("name", sorted(CAPTURES))
def test_parse_bonding_captures(name):
    primary, backup, lfc = CAPTURES[name]
    b = parse_bonding((FIXTURES / name).read_text())
    assert b.primary == primary
    assert b.active == primary
    assert b.mii_up
    assert set(b.slaves) == {primary, backup}
    assert b.slaves[primary].speed_mbps == 10000
    assert b.slaves[backup].speed_mbps == 1000
    assert b.slaves[primary].lfc == lfc
    assert b.slaves[backup].lfc == 0


def _bond_dir(tmp_path, text):
    d = tmp_path / "bonding"
    d.mkdir()
    (d / "bond0").write_text(text)
    return d


@pytest.mark.parametrize("name", sorted(CAPTURES))
def test_read_link_on_primary_is_10g(tmp_path, name):
    primary, _, lfc = CAPTURES[name]
    link = read_link(NetConfig(), proc_bonding=_bond_dir(tmp_path, (FIXTURES / name).read_text()))
    assert (link.bond, link.fabric_if, link.leg, link.speed_mbps) == (True, primary, "10g", 10000)
    assert link.lfc == lfc
    assert link.drop_counter == lfc


def test_read_link_on_backup_is_1g_and_keeps_primary_counters(tmp_path):
    text = (FIXTURES / "kubs0-aqc113.txt").read_text()
    text = text.replace("Currently Active Slave: enp8s0", "Currently Active Slave: enp7s0")
    text = text.replace(
        "Slave Interface: enp8s0\nMII Status: up\nSpeed: 10000 Mbps",
        "Slave Interface: enp8s0\nMII Status: down\nSpeed: Unknown",
    )
    link = read_link(NetConfig(), proc_bonding=_bond_dir(tmp_path, text))
    assert link.leg == "1g"
    assert link.speed_mbps == 1000
    # The card is about the 10G link: its counters stay the primary's.
    assert link.fabric_if == "enp8s0"
    assert link.lfc == 28


def test_read_link_no_active_slave_is_down(tmp_path):
    text = (FIXTURES / "kai-aqc113.txt").read_text()
    text = text.replace("Currently Active Slave: enp12s0", "Currently Active Slave: None")
    text = text.replace("MII Status: up\nMII Polling", "MII Status: down\nMII Polling")
    link = read_link(NetConfig(), proc_bonding=_bond_dir(tmp_path, text))
    assert (link.leg, link.speed_mbps) == ("down", None)


def test_read_link_without_bond_reads_the_default_route_nic(tmp_path, monkeypatch):
    sysnet = tmp_path / "net"
    (sysnet / "eth0").mkdir(parents=True)
    (sysnet / "eth0" / "carrier").write_text("1\n")
    (sysnet / "eth0" / "speed").write_text("1000\n")
    (sysnet / "eth0" / "carrier_down_count").write_text("0\n")
    monkeypatch.setattr(net, "default_route_iface", lambda: "eth0")
    link = read_link(NetConfig(), proc_bonding=tmp_path / "nobonding", sys_net=sysnet)
    assert (link.bond, link.fabric_if, link.leg, link.speed_mbps) == (False, "eth0", "1g", 1000)
    assert link.lfc is None  # no bond, no such counter: never 0
    assert link.drop_counter == 0


@pytest.mark.parametrize(
    "speed,up,leg",
    [
        (10000, True, "10g"),
        (25000, True, "10g"),
        (5000, True, "5g"),
        (2500, True, "1g"),
        (1000, True, "1g"),
        (100, True, "1g"),
        (10000, False, "down"),
        (None, True, "down"),
        (-1, True, "down"),
    ],
)
def test_classify_leg(speed, up, leg):
    assert classify_leg(speed, up) == leg


def test_parse_ping_rtt():
    out = "64 bytes from 192.168.1.1: icmp_seq=1 ttl=64 time=0.412 ms\n"
    assert parse_ping_rtt(out) == pytest.approx(0.412)
    assert parse_ping_rtt("64 bytes from x: icmp_seq=1 ttl=64 time<1 ms") == 1.0
    assert parse_ping_rtt("1 packets transmitted, 0 received") is None


def test_probe_all_bounds_a_dead_target(monkeypatch):
    """A target that never answers is killed at the deadline, not waited on."""
    import subprocess
    import time

    real = subprocess.Popen

    def fake_popen(argv, **kw):
        target = argv[-1]
        if target == "dead":
            return real(["sleep", "30"], **kw)
        return real(["echo", "64 bytes from x: icmp_seq=1 ttl=64 time=0.5 ms"], **kw)

    monkeypatch.setattr(net.subprocess, "Popen", fake_popen)
    t0 = time.monotonic()
    out = net.probe_all(["ok", "dead"], deadline_s=0.5)
    assert time.monotonic() - t0 < 2.0
    assert out == {"ok": 0.5, "dead": None}


# ---- Producer memory --------------------------------------------------------------

T0 = 1_793_000_000.0


def test_delta_is_null_until_there_is_a_window(tmp_path):
    h = NetHistory(tmp_path / "h.json")
    mem = h.observe(T0, lfc=5, rx=0, drop_counter=5)
    assert mem["lfc_delta_24h"] is None
    assert mem["rx_errors_delta_24h"] is None
    mem = h.observe(T0 + 5, lfc=5, rx=0, drop_counter=5)
    assert mem["lfc_delta_24h"] == 0


def test_delta_counts_increments_and_resets(tmp_path):
    h = NetHistory(tmp_path / "h.json")
    h.observe(T0, lfc=5, rx=10, drop_counter=5)
    h.observe(T0 + 400, lfc=7, rx=10, drop_counter=7)
    # Reboot: the counter restarts from 0, then one more failure.
    mem = h.observe(T0 + 800, lfc=1, rx=3, drop_counter=1)
    assert mem["lfc_delta_24h"] == 2 + 1
    assert mem["rx_errors_delta_24h"] == 3


def test_delta_window_is_24h(tmp_path):
    h = NetHistory(tmp_path / "h.json")
    h.observe(T0, lfc=0, rx=0, drop_counter=None)
    h.observe(T0 + 3600, lfc=4, rx=0, drop_counter=None)
    t = T0 + 3600
    while t < T0 + 3600 + 86400 + 600:
        t += 300
        mem = h.observe(t, lfc=4, rx=0, drop_counter=None)
    assert mem["lfc_delta_24h"] == 0  # the +4 fell out of the window


def test_history_survives_a_restart(tmp_path):
    """The point of the on-disk ring: no +0 after a restart."""
    p = tmp_path / "h.json"
    h = NetHistory(p)
    h.observe(T0, lfc=10, rx=0, drop_counter=10)
    h.observe(T0 + 400, lfc=13, rx=0, drop_counter=13)
    h2 = NetHistory(p)
    mem = h2.observe(T0 + 410, lfc=13, rx=0, drop_counter=13)
    assert mem["lfc_delta_24h"] == 3
    assert mem["drops"] == [T0 + 400] * 3


def test_drops_are_counter_increments_within_the_hour(tmp_path):
    h = NetHistory(tmp_path / "h.json")
    h.observe(T0, lfc=1, rx=0, drop_counter=1)
    assert h.observe(T0 + 5, lfc=2, rx=0, drop_counter=2)["drops"] == [T0 + 5]
    assert h.observe(T0 + 10, lfc=2, rx=0, drop_counter=2)["drops"] == [T0 + 5]
    assert h.observe(T0 + 3700, lfc=2, rx=0, drop_counter=2)["drops"] == []


def test_increment_across_a_long_gap_is_not_placed(tmp_path):
    h = NetHistory(tmp_path / "h.json")
    h.observe(T0, lfc=1, rx=0, drop_counter=1)
    mem = h.observe(T0 + 7200, lfc=3, rx=0, drop_counter=3)
    assert mem["drops"] == []  # it happened, but not knowably within the hour
    assert mem["lfc_delta_24h"] == 2  # ...and still counts toward the day


def test_drops_are_capped(tmp_path):
    h = NetHistory(tmp_path / "h.json")
    h.observe(T0, lfc=0, rx=0, drop_counter=0)
    assert len(h.observe(T0 + 5, lfc=500, rx=0, drop_counter=500)["drops"]) == 64


def test_corrupt_history_starts_empty(tmp_path):
    p = tmp_path / "h.json"
    p.write_text("{not json")
    h = NetHistory(p)
    assert h.observe(T0, lfc=1, rx=0, drop_counter=1)["lfc_delta_24h"] is None


def test_history_ring_stays_small(tmp_path):
    h = NetHistory(tmp_path / "h.json")
    t = T0
    for i in range(int(86400 / 5)):
        t = T0 + i * 5
        h.observe(t, lfc=1, rx=i // 1000, drop_counter=1)
    assert len(h.lfc) < 300 and len(h.rx) < 300


# ---- Collector: every record validates against the contract ---------------------


def _collector(monkeypatch, tmp_path, link, rtts, rx_errors=0):
    sysnet = tmp_path / "sysnet"
    if link.fabric_if:
        st = sysnet / link.fabric_if / "statistics"
        st.mkdir(parents=True)
        (st / "rx_errors").write_text(f"{rx_errors}\n")
        (st / "rx_bytes").write_text("1000\n")
        (st / "tx_bytes").write_text("1000\n")
    monkeypatch.setattr(net, "SYS_NET", sysnet)
    monkeypatch.setattr(net, "read_link", lambda cfg: link)
    monkeypatch.setattr(net, "probe_all", lambda targets: {t: rtts.get(t) for t in targets})
    monkeypatch.setattr(net, "local_ipv4s", lambda: {"192.168.1.111"})
    monkeypatch.setattr(net, "nic_label", lambda iface: "AQC113")
    return NetCollector(NetConfig(), history=NetHistory(tmp_path / "h.json"))


HEALTHY = {"192.168.1.1": 0.3, "1.1.1.1": 12.0, "192.168.1.235": 0.2}


def test_collector_bonded_record_validates(monkeypatch, tmp_path, validate):
    link = net.LinkState(True, "enp12s0", "10g", 10000, 1, 1)
    c = _collector(monkeypatch, tmp_path, link, HEALTHY)
    rec = c.collect()
    assert validate(rec) == []
    assert rec["leg"] == "10g" and rec["bond"] is True and rec["lfc"] == 1
    assert rec["reach"] == {"udm": True, "peers": True, "wan": True}
    assert rec["nic"] == "AQC113"
    rec = c.collect()
    assert rec["lfc_delta_24h"] == 0
    assert validate(rec) == []


def test_collector_never_probes_itself(monkeypatch, tmp_path):
    seen = []
    link = net.LinkState(True, "enp12s0", "10g", 10000, 1, 1)
    c = _collector(monkeypatch, tmp_path, link, HEALTHY)
    monkeypatch.setattr(net, "probe_all", lambda t: seen.extend(t) or {})
    c.collect()
    assert "192.168.1.111" not in seen
    assert "192.168.1.1" in seen and "1.1.1.1" in seen


def test_collector_single_nic_record_validates(monkeypatch, tmp_path, validate):
    """rpi53: no bond, so lfc and its delta are present-and-null (CD-27 ruling 4)."""
    link = net.LinkState(False, "eth0", "1g", 1000, None, 0)
    rec = _collector(monkeypatch, tmp_path, link, HEALTHY).collect()
    assert validate(rec) == []
    assert rec["bond"] is False
    assert "lfc" in rec and rec["lfc"] is None
    assert "lfc_delta_24h" in rec and rec["lfc_delta_24h"] is None


def test_collector_uplink_lost_record_validates(monkeypatch, tmp_path, validate):
    """Peers answer, the UDM and the Internet do not: RTTs null exactly then."""
    link = net.LinkState(True, "enp12s0", "10g", 10000, 2, 2)
    rec = _collector(monkeypatch, tmp_path, link, {"192.168.1.235": 0.2}).collect()
    assert validate(rec) == []
    assert rec["reach"] == {"udm": False, "peers": True, "wan": False}
    assert rec["rtt_ms"] == {"udm": None, "wan": None}


def test_collector_down_record_validates(monkeypatch, tmp_path, validate):
    link = net.LinkState(True, "enp12s0", "down", None, 5, 5)
    rec = _collector(monkeypatch, tmp_path, link, {}).collect()
    assert validate(rec) == []
    assert rec["speed_mbps"] is None
    assert rec["reach"] == {"udm": False, "peers": False, "wan": False}


def test_collector_unreadable_counter_is_null_not_zero(monkeypatch, tmp_path, validate):
    link = net.LinkState(True, "enp12s0", "10g", 10000, 1, 1)
    c = _collector(monkeypatch, tmp_path, link, HEALTHY)
    (tmp_path / "sysnet" / "enp12s0" / "statistics" / "rx_errors").unlink()
    rec = c.collect()
    assert validate(rec) == []
    assert rec["rx_errors"] is None and rec["rx_errors_delta_24h"] is None


# ---- Redis + config ----------------------------------------------------------------


def test_write_net_uses_the_contract_key_and_ttl(tmp_path):
    cfg = tmp_path / "config.toml"
    cfg.write_text("[redis]\nhost = '127.0.0.1'\n")
    client = RedisClient(ClientConfig.load(cfg))
    client._r = MagicMock()
    client._hostname = "kai"
    client.write_net({"leg": "10g"})
    (key, value), kw = client._r.set.call_args
    assert key == "kdash:net:kai"
    assert kw == {"ex": 15}
    assert json.loads(value) == {"leg": "10g"}


def test_net_config_defaults_and_overrides(tmp_path):
    p = tmp_path / "config.toml"
    p.write_text("")
    cfg = ClientConfig.load(p).net
    assert cfg.udm == "192.168.1.1" and cfg.wan == "1.1.1.1"
    assert "192.168.1.235" in cfg.peers
    p.write_text(
        "[net]\nenabled = false\nwan = '9.9.9.9'\npeers = ['10.0.0.2']\nnic = 'X550'\n"
    )
    cfg = ClientConfig.load(p).net
    assert (cfg.enabled, cfg.wan, cfg.peers, cfg.nic) == (False, "9.9.9.9", ["10.0.0.2"], "X550")


def test_net_config_rejects_a_bad_peer_list(tmp_path):
    p = tmp_path / "config.toml"
    p.write_text("[net]\npeers = '10.0.0.2'\n")
    with pytest.raises(ConfigError):
        ClientConfig.load(p)
