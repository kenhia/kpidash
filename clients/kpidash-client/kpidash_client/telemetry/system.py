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
    """Return OS name string, e.g. 'Linux 5.15.0-173-generic'."""
    return f"{platform.system()} {platform.release()}"
