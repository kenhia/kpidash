"""
config.py — ClientConfig dataclass loaded from TOML (T011)

Config file location:
  Linux/macOS: ~/.config/kpidash-client/config.toml
  Windows:     %APPDATA%\\kpidash-client\\config.toml
"""

from __future__ import annotations

import platform
import tomllib
from dataclasses import dataclass, field
from pathlib import Path


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
class RepoConfig:
    explicit: list[str] = field(default_factory=list)
    scan_roots: list[str] = field(default_factory=list)
    exclude: list[str] = field(default_factory=list)
    scan_depth: int = 3


@dataclass
class ClientConfig:
    # Required
    redis_host: str
    # Optional with defaults
    redis_port: int = 6379
    hostname: str | None = None  # overrides socket hostname
    telemetry_interval_s: int = 5  # A1: configurable telemetry interval
    dev_interval_s: int | None = None  # fast GPU+CPU+RAM interval (default: telemetry_interval_s)
    health_interval_s: int = 3  # health ping interval
    repo_scan_interval_s: int = 30  # repo scan interval
    disks: list[DiskConfig] = field(default_factory=list)
    repos: RepoConfig = field(default_factory=RepoConfig)

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

        redis_section = data.get("redis", {})
        if "host" not in redis_section:
            raise ConfigError(
                f"[redis] host is required in {p}\nExample: [redis]\\nhost = '192.168.1.100'"
            )

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

        # Parse repos
        repos_data = data.get("repos", {})
        repos = RepoConfig(
            explicit=repos_data.get("explicit", []),
            scan_roots=repos_data.get("scan_roots", []),
            exclude=repos_data.get("exclude", []),
            scan_depth=repos_data.get("scan_depth", 3),
        )

        # Parse client section
        client_section = data.get("client", {})

        return cls(
            redis_host=redis_section["host"],
            redis_port=int(redis_section.get("port", 6379)),
            hostname=client_section.get("hostname"),
            telemetry_interval_s=int(client_section.get("telemetry_interval_s", 5)),
            dev_interval_s=int(client_section["dev_interval_s"])
            if "dev_interval_s" in client_section
            else None,
            health_interval_s=int(client_section.get("health_interval_s", 3)),
            repo_scan_interval_s=int(client_section.get("repo_scan_interval_s", 30)),
            disks=disks,
            repos=repos,
        )
