"""tests/test_nas.py -- kdash:nas:{host} v1 producer (WI #3956).

Validated against kdashdata's own schema (the submodule the gate already
initialises) with a small walker for the subset of JSON Schema it uses, so a
contract change shows up here rather than as a blank bar on rpi53.
"""

from __future__ import annotations

import json
from pathlib import Path

import pytest

from kpidash_client.config import ClientConfig
from kpidash_client.nas import NasCollector, NasConfig, iso_to_unix, project

HERE = Path(__file__).parent
FIXTURE = HERE / "fixtures" / "gratch-privileged-2026-11-10.json"
SCHEMA = HERE.parents[2] / "lib" / "kdashdata" / "contracts" / "schemas" / "kdash-nas.schema.json"

_TYPES = {
    "object": dict,
    "array": list,
    "string": str,
    "boolean": bool,
    "integer": int,
    "number": (int, float),
    "null": type(None),
}


def _check(value, schema, path="$"):
    """Raise AssertionError on the first violation of the schema subset in use."""
    types = schema.get("type")
    if types is not None:
        names = types if isinstance(types, list) else [types]
        ok = any(
            isinstance(value, _TYPES[t])
            and not (t in ("integer", "number") and value is True)
            and not (t in ("integer", "number") and value is False)
            for t in names
        )
        assert ok, f"{path}: {value!r} is not {names}"
    if value is None:
        return
    if "enum" in schema:
        assert value in schema["enum"], f"{path}: {value!r} not in {schema['enum']}"
    if isinstance(value, int | float) and not isinstance(value, bool):
        if "minimum" in schema:
            assert value >= schema["minimum"], path
        if "exclusiveMinimum" in schema:
            assert value > schema["exclusiveMinimum"], path
        if "maximum" in schema:
            assert value <= schema["maximum"], path
    if isinstance(value, str) and "minLength" in schema:
        assert len(value) >= schema["minLength"], path
    if isinstance(value, dict):
        for key in schema.get("required", []):
            assert key in value, f"{path}: missing {key}"
        for key, sub in schema.get("properties", {}).items():
            if key in value:
                _check(value[key], sub, f"{path}.{key}")
    if isinstance(value, list):
        if "maxItems" in schema:
            assert len(value) <= schema["maxItems"], path
        for i, item in enumerate(value):
            _check(item, schema.get("items", {}), f"{path}[{i}]")


@pytest.fixture(scope="module")
def schema():
    return json.loads(SCHEMA.read_text())


@pytest.fixture(scope="module")
def gratch():
    return json.loads(FIXTURE.read_text())


def test_the_schema_walker_rejects_the_schemas_own_counterexamples(schema):
    """A walker that passes everything proves nothing: every counterexample
    the contract ships must fail it."""
    for example in schema["examples"]:
        _check(example, schema)
    failed = 0
    for counter in schema["x-counterexamples"]:
        try:
            _check(counter["record"], schema)
        except AssertionError:
            failed += 1
    # The ATA `percentage_used` omission is a required-key check; all five fail.
    assert failed == len(schema["x-counterexamples"])


def test_gratchs_real_file_projects_to_a_valid_record(schema, gratch):
    record = project(gratch, now=1793163800.2)
    _check(record, schema)
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


def test_a_missing_source_still_publishes_collector_down(schema):
    record = project(None, now=1793163800.2)
    _check(record, schema)
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
