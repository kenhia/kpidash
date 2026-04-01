"""telemetry/disk.py — Disk usage collection (T023)

Linux: auto-detects disk type via /sys/block.
Windows: disk type must be supplied in config (no auto-detection).
"""

from __future__ import annotations

import platform
import re
from pathlib import Path

import psutil

from ..config import DiskConfig


def _rotational_for_base(base_dev: str) -> str | None:
    """
    Read /sys/block/{base_dev}/queue/rotational.
    For device-mapper / LVM nodes (no direct /sys/block entry), walk
    /sys/block/dm-*/slaves/ to find the real underlying block device.
    Returns 'hdd', 'ssd', or None.
    """
    rot_path = Path(f"/sys/block/{base_dev}/queue/rotational")
    if rot_path.exists():
        val = rot_path.read_text().strip()
        return "hdd" if val == "1" else "ssd"

    # LVM / device-mapper: find a dm-* device whose slaves/ contain base_dev
    for dm in Path("/sys/block").glob("dm-*"):
        slaves_dir = dm / "slaves"
        if not slaves_dir.exists():
            continue
        slaves = [s.name for s in slaves_dir.iterdir()]
        # The dm device might back a node named differently; also try the
        # mapper name via dm/dm/name
        dm_name_file = dm / "dm" / "name"
        dm_name = dm_name_file.read_text().strip() if dm_name_file.exists() else ""
        if base_dev in slaves or base_dev == dm_name:
            # Pick the first slave and recurse once
            for slave in slaves:
                if "nvme" in slave:
                    return "nvme"
                slave_base = re.sub(r"\d+$", "", slave)
                result = _rotational_for_base(slave_base)
                if result:
                    return result
    return None


def _linux_detect_type(path: str) -> str | None:
    """
    Try to detect disk type from /sys/block on Linux.
    Returns 'nvme', 'ssd', 'hdd', or None if detection fails.
    Handles plain block devices, NVMe, and LVM/device-mapper volumes.
    """
    try:
        # Find the best-matching (longest mountpoint) partition for path
        dev = None
        best_len = 0
        partitions = psutil.disk_partitions(all=True)
        for p in partitions:
            if (p.mountpoint == path or path.startswith(p.mountpoint)) and len(p.mountpoint) > best_len:
                dev = p.device.split("/")[-1]
                best_len = len(p.mountpoint)
        if not dev:
            return None

        # NVMe: device name contains 'nvme'
        if "nvme" in dev:
            return "nvme"

        # Strip partition number suffix (sda1 → sda, nvme0n1p1 already caught above)
        base_dev = re.sub(r"\d+$", "", dev)
        return _rotational_for_base(base_dev)
    except Exception:
        pass
    return None


def collect_disks(disk_configs: list[DiskConfig]) -> list[dict]:
    """
    Collect disk usage for each configured disk.

    Each config entry supplies a mount path; type is auto-detected on
    Linux or taken from config on Windows (A2).
    """
    results = []
    is_linux = platform.system() == "Linux"

    for dc in disk_configs:
        try:
            usage = psutil.disk_usage(dc.path)
            used_gb = usage.used / (1024**3)
            total_gb = usage.total / (1024**3)
            pct = usage.percent

            # Determine type
            dtype = dc.type
            if is_linux and not dtype:
                dtype = _linux_detect_type(dc.path) or "other"
            if not dtype:
                dtype = "other"

            results.append(
                {
                    "label": dc.label,
                    "type": dtype,
                    "used_gb": round(used_gb, 1),
                    "total_gb": round(total_gb, 1),
                    "pct": round(pct, 1),
                }
            )
        except Exception:
            # Skip disks that can't be read (unmounted, permission denied)
            pass

    return results
