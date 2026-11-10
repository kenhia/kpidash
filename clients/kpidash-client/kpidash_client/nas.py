"""nas.py — kdash:nas:{host} v1, the NAS big panel's health block (WI #3956).

A projection of gratch-ui's root collector output, `/run/gratch-ui/privileged.json`
(korg:3945), field for field per kdashdata's registry.md mapping table. This
module re-reads that file and **never runs smartctl**: gratch-ui is the one
place that knows how to leave a standby IronWolf asleep, and it stays the only
one.

Every value the source could not supply is present-and-null, never absent
(CD-27). A missing or unreadable source still publishes — `collected_ts: null`,
`source_stale: true`, empty arrays — so the panel says "collector down" rather
than going blank.
"""

from __future__ import annotations

import json
import re
import time
from dataclasses import dataclass
from datetime import UTC, datetime
from pathlib import Path

DEFAULT_SOURCE = "/run/gratch-ui/privileged.json"

POOL_HEALTH = ("ONLINE", "DEGRADED", "FAULTED", "OFFLINE", "UNAVAIL", "REMOVED", "SUSPENDED")
SCRUB_STATES = ("scanning", "finished", "canceled")


@dataclass
class NasConfig:
    """`[nas]`. `enabled` None is auto: publish once the source has existed
    during this run, so only the host running gratch-ui ever writes the key —
    and a client that starts before the collector's first write still picks
    it up. True publishes from the start, even with no source."""

    enabled: bool | None = None
    source: str = DEFAULT_SOURCE


_FRACTION = re.compile(r"(\.\d{6})\d+")


def iso_to_unix(value: object) -> float | None:
    """`2026-11-10T01:51:55.984952943Z` → unix seconds. The source writes
    nanoseconds; datetime takes microseconds, so the excess is trimmed."""
    if not isinstance(value, str) or not value:
        return None
    text = _FRACTION.sub(r"\1", value.strip())
    if text.endswith("Z"):
        text = text[:-1] + "+00:00"
    try:
        stamp = datetime.fromisoformat(text)
    except ValueError:
        return None
    if stamp.tzinfo is None:
        stamp = stamp.replace(tzinfo=UTC)
    return stamp.timestamp()


def _int_or_none(value: object) -> int | None:
    if isinstance(value, bool) or not isinstance(value, int):
        return None
    return value if value >= 0 else None


def _num_or_none(value: object) -> float | int | None:
    if isinstance(value, bool) or not isinstance(value, int | float):
        return None
    return value


def _scrub(scan: object) -> dict | None:
    """`pools[].scan` when its `function` is SCRUB; a resilver or an expansion
    is not a scrub, so it reads as none."""
    if not isinstance(scan, dict) or scan.get("function") != "SCRUB":
        return None
    state = str(scan.get("state", "")).lower()
    if state not in SCRUB_STATES:
        return None
    return {
        "state": state,
        "end_ts": None if state == "scanning" else iso_to_unix(scan.get("end")),
        "errors": _int_or_none(scan.get("errors")) or 0,
    }


def _pool(src: dict) -> dict | None:
    name = src.get("name")
    used, avail = _int_or_none(src.get("used_bytes")), _int_or_none(src.get("avail_bytes"))
    if not isinstance(name, str) or not name or used is None or avail is None:
        return None
    health = src.get("health")
    return {
        "name": name,
        # ZFS's own closed vocabulary, verbatim. Anything else is a source
        # change, shown as the worst state rather than passed through blank.
        "health": health if health in POOL_HEALTH else "UNAVAIL",
        "used_bytes": used,
        "avail_bytes": avail,
        "errors": _int_or_none(src.get("error_count")),
        "scrub": _scrub(src.get("scan")),
    }


def _kind(src: dict) -> str:
    if src.get("protocol") == "NVMe":
        return "nvme"
    return "hdd" if src.get("rotational") is True else "ssd"


def _power_state(value: object) -> str:
    if value in ("active", "idle"):
        return "active"
    if value == "standby":
        return "standby"
    return "unknown"


def _disk(src: dict) -> dict | None:
    device = src.get("device")
    if not isinstance(device, str) or not device:
        return None
    passed = src.get("smart_passed")
    model = src.get("model")
    return {
        "device": device,
        "model": model if isinstance(model, str) else "",
        "kind": _kind(src),
        "power_state": _power_state(src.get("power_state")),
        "smart_passed": passed if isinstance(passed, bool) else None,
        "smart_ts": iso_to_unix(src.get("smart_read_at")),
        "temp_c": _num_or_none(src.get("temp_c")),
        "reallocated_sectors": _int_or_none(src.get("reallocated_sectors")),
        "pending_sectors": _int_or_none(src.get("pending_sectors")),
        "percentage_used": _int_or_none(src.get("percentage_used")),
        "media_errors": _int_or_none(src.get("media_errors")),
    }


def project(source: object, now: float | None = None) -> dict:
    """The kdash:nas v1 record for one parsed `privileged.json` (None when the
    file was missing or unreadable)."""
    ts = time.time() if now is None else now
    if not isinstance(source, dict):
        return {
            "collected_ts": None,
            "source_stale": True,
            "source_version": None,
            "pools": [],
            "disks": [],
            "ts": ts,
        }
    pools = [p for p in (_pool(x) for x in source.get("pools") or [] if isinstance(x, dict)) if p]
    disks = [d for d in (_disk(x) for x in source.get("disks") or [] if isinstance(x, dict)) if d]
    version = source.get("version")
    return {
        "collected_ts": iso_to_unix(source.get("collected_at")),
        "source_stale": source.get("stale") is not False,
        "source_version": version if isinstance(version, str) else None,
        "pools": pools[:16],
        "disks": disks[:32],
        "ts": ts,
    }


class NasCollector:
    """Reads the source each tick; `collect()` returns None while auto mode
    has not yet seen the file, which means "publish nothing"."""

    def __init__(self, config: NasConfig) -> None:
        self._config = config
        self._seen = config.enabled is True

    def collect(self, now: float | None = None) -> dict | None:
        path = Path(self._config.source)
        try:
            source = json.loads(path.read_text())
            self._seen = True
        except FileNotFoundError:
            source = None
        except (OSError, ValueError):
            # Present but unreadable: that IS the NAS host, collector broken.
            self._seen = True
            source = None
        if not self._seen:
            return None
        return project(source, now)
