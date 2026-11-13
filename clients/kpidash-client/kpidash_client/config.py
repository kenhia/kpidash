"""
config.py — ClientConfig dataclass loaded from TOML (T011)

Config file location:
  Linux/macOS: ~/.config/kpidash-client/config.toml
  Windows:     %APPDATA%\\kpidash-client\\config.toml
"""

from __future__ import annotations

import logging
import platform
import tomllib
from dataclasses import dataclass, field
from pathlib import Path

from .nas import NasConfig
from .net import NetConfig

# How the host is expected to be present (WI #3132). `intermittent` is a host that
# sleeps by design -- a Mac, a laptop -- and the dashboard renders its silence as
# asleep rather than as a fault. Mirrors k-homelab inventory's `availability:`.
AVAILABILITY_VALUES = ("always", "intermittent")


class ConfigError(ValueError):
    """Raised when the configuration is invalid or missing required fields."""


def _config_path() -> Path:
    if platform.system() == "Windows":
        import os

        appdata = os.environ.get("APPDATA", Path.home() / "AppData" / "Roaming")
        return Path(appdata) / "kpidash-client" / "config.toml"
    return Path.home() / ".config" / "kpidash-client" / "config.toml"


@dataclass
class DiskConfig:
    path: str
    label: str
    type: str | None = None  # Required on Windows; auto-detected on Linux


@dataclass
class ClientConfig:
    # Redis endpoint. Both are an explicit LOCAL OVERRIDE: leave `[redis]
    # host` out of config.toml and the endpoint is resolved through khlenv
    # instead (see endpoint.py), which is how Redis moves with one store
    # edit rather than a config edit on every host.
    redis_host: str | None = None
    redis_port: int | None = None
    # Optional with defaults
    hostname: str | None = None  # overrides socket hostname
    telemetry_interval_s: int = 5  # A1: configurable telemetry interval
    dev_interval_s: int | None = None  # fast GPU+CPU+RAM interval (default: telemetry_interval_s)
    health_interval_s: int = 3  # health ping interval
    availability: str = "always"  # "always" | "intermittent" (AVAILABILITY_VALUES)
    disks: list[DiskConfig] = field(default_factory=list)
    # WI #4061: kdash:net:{host}. On by default on Linux, the only platform
    # whose inputs (/proc/net/bonding, /sys/class/net) the collector reads.
    net: NetConfig = field(
        default_factory=lambda: NetConfig(enabled=platform.system() == "Linux")
    )
    # WI #3956: kdash:nas:{host}. Auto by default — only a host where gratch-ui's
    # collector file appears ever publishes.
    nas: NasConfig = field(default_factory=NasConfig)

    @classmethod
    def load(cls, path: Path | None = None) -> ClientConfig:
        """Load ClientConfig from TOML. Raises ConfigError on invalid config."""
        p = path or _config_path()
        if not p.exists():
            raise ConfigError(
                f"Config file not found: {p}\nCreate it at {p} — see README for an example."
            )
        try:
            with open(p, "rb") as f:
                data = tomllib.load(f)
        except tomllib.TOMLDecodeError as e:
            raise ConfigError(f"Config parse error in {p}: {e}") from e

        # `[redis] host` is optional: absent means "resolve through khlenv".
        # Present, it overrides khlenv entirely, port included.
        redis_section = data.get("redis", {})
        redis_host = redis_section.get("host")
        if redis_host is None:
            redis_port = None
            if "port" in redis_section:
                logging.getLogger(__name__).warning(
                    "[redis] port in %s is ignored without [redis] host — the endpoint "
                    "is being resolved through khlenv, port and all.",
                    p,
                )
        else:
            redis_port = int(redis_section.get("port", 6379))

        # Parse disks
        disks = []
        for d in data.get("disks", []):
            if "path" not in d:
                raise ConfigError("Each [[disks]] entry requires 'path'")
            disks.append(
                DiskConfig(
                    path=d["path"],
                    label=d.get("label", d["path"].split("/")[-1] or d["path"]),
                    type=d.get("type"),
                )
            )

        # `[repos]` and `[client] repo_scan_interval_s` configured the repo scanner,
        # removed in 2.0.0 (sprint 036). They are ignored, silently: k-homelab still
        # renders them until its own follow-up lands, and a warning on every start
        # of every host would be noise about a file the host does not own.

        # Parse client section
        client_section = data.get("client", {})

        availability = client_section.get("availability", "always")
        if availability not in AVAILABILITY_VALUES:
            raise ConfigError(
                f"[client] availability = {availability!r} in {p} -- expected one of "
                + ", ".join(AVAILABILITY_VALUES)
            )

        net = _parse_net(data.get("net", {}), p)
        nas = _parse_nas(data.get("nas", {}), p)

        return cls(
            redis_host=redis_host,
            redis_port=redis_port,
            hostname=client_section.get("hostname"),
            telemetry_interval_s=int(client_section.get("telemetry_interval_s", 5)),
            dev_interval_s=int(client_section["dev_interval_s"])
            if "dev_interval_s" in client_section
            else None,
            health_interval_s=int(client_section.get("health_interval_s", 3)),
            availability=availability,
            disks=disks,
            net=net,
            nas=nas,
        )


def _parse_net(section: dict, p: Path) -> NetConfig:
    """`[net]` — every key optional; the defaults describe the 10G fabric."""
    cfg = NetConfig(enabled=platform.system() == "Linux")
    if "enabled" in section:
        cfg.enabled = bool(section["enabled"])
    for key in ("bond", "udm", "wan", "nic", "state_dir"):
        if key in section:
            if not isinstance(section[key], str):
                raise ConfigError(f"[net] {key} in {p} must be a string")
            setattr(cfg, key, section[key])
    if "peers" in section:
        peers = section["peers"]
        if not isinstance(peers, list) or not all(isinstance(x, str) for x in peers):
            raise ConfigError(f"[net] peers in {p} must be a list of addresses")
        cfg.peers = list(peers)
    return cfg


def _parse_nas(section: dict, p: Path) -> NasConfig:
    """`[nas]` — `enabled` (absent = auto) and `source`, both optional."""
    cfg = NasConfig()
    if "enabled" in section:
        cfg.enabled = bool(section["enabled"])
    if "source" in section:
        if not isinstance(section["source"], str):
            raise ConfigError(f"[nas] source in {p} must be a string")
        cfg.source = section["source"]
    return cfg
