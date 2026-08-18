"""tests/test_config.py — Unit tests for ClientConfig (T013)"""

from __future__ import annotations

import logging

import pytest

from kpidash_client.config import ClientConfig, ConfigError

VALID_TOML = """
[redis]
host = "192.168.1.100"
port = 6380

[client]
telemetry_interval_s = 10

[[disks]]
path = "/dev/sda1"
label = "root"
type = "nvme"

[repos]
explicit = ["/home/ken/src/kpidash"]
scan_roots = ["/home/ken/src"]
exclude = ["/home/ken/src/vendor"]
scan_depth = 2
"""

MINIMAL_TOML = """
[redis]
host = "10.0.0.1"
"""


def test_valid_load(tmp_path):
    cfg_file = tmp_path / "config.toml"
    cfg_file.write_text(VALID_TOML)
    cfg = ClientConfig.load(cfg_file)

    assert cfg.redis_host == "192.168.1.100"
    assert cfg.redis_port == 6380
    assert cfg.telemetry_interval_s == 10
    assert len(cfg.disks) == 1
    assert cfg.disks[0].path == "/dev/sda1"
    assert cfg.disks[0].label == "root"
    assert cfg.disks[0].type == "nvme"
    assert "/home/ken/src/kpidash" in cfg.repos.explicit
    assert cfg.repos.scan_depth == 2


def test_minimal_load(tmp_path):
    cfg_file = tmp_path / "config.toml"
    cfg_file.write_text(MINIMAL_TOML)
    cfg = ClientConfig.load(cfg_file)

    assert cfg.redis_host == "10.0.0.1"
    assert cfg.redis_port == 6379  # default
    assert cfg.telemetry_interval_s == 5  # default
    assert cfg.disks == []
    assert cfg.repos.explicit == []


def test_absent_redis_host_means_resolve_through_khlenv(tmp_path):
    """No `[redis] host` is not an error any more -- it hands the endpoint
    to khlenv, which is the whole point of the wiring."""
    cfg_file = tmp_path / "config.toml"
    cfg_file.write_text("[client]\nhostname = 'kubs0'\n")
    cfg = ClientConfig.load(cfg_file)

    assert cfg.redis_host is None
    assert cfg.redis_port is None
    assert cfg.hostname == "kubs0"


def test_a_port_without_a_host_is_ignored_and_warned_about(tmp_path, caplog):
    cfg_file = tmp_path / "config.toml"
    cfg_file.write_text("[redis]\nport = 6380\n")
    with caplog.at_level(logging.WARNING):
        cfg = ClientConfig.load(cfg_file)

    assert cfg.redis_host is None
    assert cfg.redis_port is None
    assert "ignored" in "\n".join(record.getMessage() for record in caplog.records)


def test_file_not_found(tmp_path):
    with pytest.raises(ConfigError, match="not found"):
        ClientConfig.load(tmp_path / "nonexistent.toml")


def test_invalid_toml(tmp_path):
    cfg_file = tmp_path / "config.toml"
    cfg_file.write_text("[[[ not valid toml")
    with pytest.raises(ConfigError, match="parse error"):
        ClientConfig.load(cfg_file)


def test_disk_missing_path(tmp_path):
    toml = "[redis]\nhost = 'x'\n[[disks]]\nlabel = 'root'\n"
    cfg_file = tmp_path / "config.toml"
    cfg_file.write_text(toml)
    with pytest.raises(ConfigError, match="path"):
        ClientConfig.load(cfg_file)


def test_defaults(tmp_path):
    cfg_file = tmp_path / "config.toml"
    cfg_file.write_text(MINIMAL_TOML)
    cfg = ClientConfig.load(cfg_file)
    assert cfg.health_interval_s == 3
    assert cfg.repo_scan_interval_s == 30
    assert cfg.hostname is None
