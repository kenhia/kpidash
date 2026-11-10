"""tests/test_nas.py -- kdash:nas:{host} v1 producer (WI #3956).

Every record shape is validated against the contract's own schema with
kdashdata's own validator, as test_net.py does — the submodule is part of the
gate, so a contract change shows up here rather than as a blank bar on rpi53.
"""

from __future__ import annotations

import json
import sys
from pathlib import Path

import pytest

from kpidash_client.config import ClientConfig
from kpidash_client.nas import NasCollector, NasConfig, iso_to_unix, project

HERE = Path(__file__).parent
FIXTURE = HERE / "fixtures" / "gratch-privileged-2026-11-10.json"
KDASHDATA = Path(__file__).resolve().parents[3] / "lib" / "kdashdata"
SCHEMA = KDASHDATA / "contracts" / "schemas" / "kdash-nas.schema.json"


@pytest.fixture(scope="module")
def validate():
    assert SCHEMA.exists(), f"{SCHEMA} missing — run `git submodule update --init lib/kdashdata`"
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


@pytest.fixture(scope="module")
def gratch():
    return json.loads(FIXTURE.read_text())


def test_gratchs_real_file_projects_to_a_valid_record(validate, gratch):
    record = project(gratch, now=1793163800.2)
    assert validate(record) == []
    assert record["source_stale"] is False
    assert record["source_version"] == "0.1.0-7f28ad6"
    assert record["collected_ts"] == pytest.approx(iso_to_unix("2026-11-10T01:51:55.984952Z"))
    assert [p["name"] for p in record["pools"]] == ["nas"]


def test_pool_usage_is_usable_not_raw(gratch):
    pool = project(gratch)["pools"][0]
    assert pool["used_bytes"] == gratch["pools"][0]["used_bytes"]
    assert pool["used_bytes"] != gratch["pools"][0]["alloc_bytes"]
    assert pool["errors"] == 0
    assert pool["scrub"]["state"] == "finished"
    assert pool["scrub"]["end_ts"] == iso_to_unix("2026-11-08T00:33:01Z")


def test_disk_projection(gratch):
    disks = {d["device"]: d for d in project(gratch)["disks"]}
    assert disks["/dev/nvme0"]["kind"] == "nvme"
    assert disks["/dev/sda"]["kind"] == "ssd"
    assert disks["/dev/sdb"]["kind"] == "hdd"
    assert disks["/dev/sdb"]["power_state"] == "standby"
    # Present-and-null where the type does not report it (CD-27).
    assert "percentage_used" in disks["/dev/sdb"] and disks["/dev/sdb"]["percentage_used"] is None
    assert disks["/dev/sda"]["pending_sectors"] is None
    assert disks["/dev/nvme0"]["reallocated_sectors"] is None
    assert disks["/dev/sdc"]["reallocated_sectors"] == 8


@pytest.mark.parametrize(
    ("src", "want"),
    [
        ("active", "active"),
        ("idle", "active"),
        ("standby", "standby"),
        ("STANDBY", "unknown"),
        (None, "unknown"),
    ],
)
def test_power_state_maps_into_the_closed_enum(src, want, gratch):
    disk = dict(gratch["disks"][0], power_state=src)
    assert project({"disks": [disk]})["disks"][0]["power_state"] == want


def test_a_resilver_is_not_a_scrub(gratch):
    pool = dict(gratch["pools"][0], scan={"function": "RESILVER", "state": "FINISHED"})
    assert project({"pools": [pool]})["pools"][0]["scrub"] is None


def test_a_running_scrub_has_no_end(gratch):
    scan = dict(gratch["pools"][0]["scan"], state="SCANNING")
    pool = dict(gratch["pools"][0], scan=scan)
    assert project({"pools": [pool]})["pools"][0]["scrub"] == {
        "state": "scanning",
        "end_ts": None,
        "errors": 0,
    }


def test_a_missing_source_still_publishes_collector_down(validate):
    record = project(None, now=1793163800.2)
    assert validate(record) == []
    assert record == {
        "collected_ts": None,
        "source_stale": True,
        "source_version": None,
        "pools": [],
        "disks": [],
        "ts": 1793163800.2,
    }


def test_auto_mode_publishes_nothing_until_the_source_appears(tmp_path):
    source = tmp_path / "privileged.json"
    collector = NasCollector(NasConfig(source=str(source)))
    assert collector.collect() is None
    source.write_text(FIXTURE.read_text())
    assert collector.collect()["pools"]
    # Once seen, a vanished source is "collector down", not silence.
    source.unlink()
    assert collector.collect()["source_stale"] is True


def test_an_unreadable_source_counts_as_seen(tmp_path):
    source = tmp_path / "privileged.json"
    source.write_text("{not json")
    record = NasCollector(NasConfig(source=str(source))).collect()
    assert record is not None and record["collected_ts"] is None


def test_enabled_publishes_without_a_source(tmp_path):
    collector = NasCollector(NasConfig(enabled=True, source=str(tmp_path / "nope.json")))
    assert collector.collect()["source_stale"] is True


def test_config_parses_nas_section(tmp_path):
    cfg = tmp_path / "config.toml"
    cfg.write_text('[nas]\nenabled = false\nsource = "/tmp/x.json"\n')
    loaded = ClientConfig.load(cfg)
    assert loaded.nas.enabled is False and loaded.nas.source == "/tmp/x.json"
    cfg.write_text("")
    assert ClientConfig.load(cfg).nas.enabled is None


@pytest.mark.parametrize(
    ("text", "want"),
    [
        ("2026-11-08T00:33:01Z", 1794097981.0),
        ("2026-11-10T01:51:55.984952943Z", 1794275515.984952),
        ("", None),
        (None, None),
        ("yesterday", None),
    ],
)
def test_iso_to_unix(text, want):
    got = iso_to_unix(text)
    assert got == (pytest.approx(want, abs=1e-5) if want is not None else None)
