"""telemetry/system.py — CPU and RAM collection via psutil (T021)"""

from __future__ import annotations

import platform

import psutil


def collect_system() -> dict:
    """
    Collect CPU and RAM metrics.

    Returns a dict matching the TelemetrySnapshot schema (cpu_pct,
    top_core_pct, ram_used_mb, ram_total_mb).
    """
    # CPU — overall (mean of per-core) + top core; single call avoids drift
    per_core: list[float] = psutil.cpu_percent(interval=None, percpu=True)
    overall = sum(per_core) / len(per_core) if per_core else 0.0
    top_core = max(per_core) if per_core else 0.0

    # RAM
    mem = psutil.virtual_memory()
    ram_total_mb = mem.total // (1024 * 1024)
    ram_used_mb = mem.used // (1024 * 1024)

    return {
        "cpu_pct": round(float(overall), 1),
        "top_core_pct": round(float(top_core), 1),
        "ram_used_mb": int(ram_used_mb),
        "ram_total_mb": int(ram_total_mb),
    }


def collect_os_name() -> str:
    """Return a human-friendly OS name.

    On Linux, reads PRETTY_NAME from /etc/os-release (e.g. 'Ubuntu 22.04.5 LTS').
    Falls back to 'Linux <kernel>' if the file is missing or unparseable.
    On other platforms returns '<system> <release>'.
    """
    if platform.system() == "Linux":
        try:
            with open("/etc/os-release") as f:
                for line in f:
                    if line.startswith("PRETTY_NAME="):
                        value = line.split("=", 1)[1].strip().strip('"')
                        if value:
                            return value
        except OSError:
            pass
    return f"{platform.system()} {platform.release()}"
