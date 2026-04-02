"""tests/test_telemetry.py — T025

Unit tests for telemetry collectors. All external libraries (psutil, pynvml)
are mocked so these tests run without hardware.
"""

from __future__ import annotations

import sys
from unittest.mock import MagicMock, patch

# ---------------------------------------------------------------------------
# collect_system
# ---------------------------------------------------------------------------


def test_collect_system_returns_expected_keys():
    cpu_values = [20.0, 15.0, 50.0, 10.0]
    vm = MagicMock()
    vm.used = 4 * 1024**3  # 4 GiB
    vm.total = 16 * 1024**3  # 16 GiB

    with (
        patch("psutil.cpu_percent", return_value=cpu_values),
        patch("psutil.virtual_memory", return_value=vm),
    ):
        from kpidash_client.telemetry.system import collect_system

        result = collect_system()

    assert "cpu_pct" in result
    assert "top_core_pct" in result
    assert "ram_used_mb" in result
    assert "ram_total_mb" in result


def test_collect_system_cpu_values():
    cpu_values = [20.0, 15.0, 50.0, 10.0]
    vm = MagicMock()
    vm.used = 0
    vm.total = 1

    with (
        patch("psutil.cpu_percent", return_value=cpu_values),
        patch("psutil.virtual_memory", return_value=vm),
    ):
        from kpidash_client.telemetry.system import collect_system

        result = collect_system()

    expected_avg = sum(cpu_values) / len(cpu_values)
    assert abs(result["cpu_pct"] - expected_avg) < 0.1  # rounded to 1 dp
    assert result["top_core_pct"] == max(cpu_values)


def test_collect_system_ram_conversion():
    vm = MagicMock()
    vm.used = 8 * 1024**3  # 8 GiB → 8192 MiB
    vm.total = 32 * 1024**3  # 32 GiB → 32768 MiB

    with (
        patch("psutil.cpu_percent", return_value=[0.0]),
        patch("psutil.virtual_memory", return_value=vm),
    ):
        from kpidash_client.telemetry.system import collect_system

        result = collect_system()

    assert result["ram_used_mb"] == 8192
    assert result["ram_total_mb"] == 32768


# ---------------------------------------------------------------------------
# collect_gpu — unavailable path
# ---------------------------------------------------------------------------


def _fresh_gpu_module() -> None:
    """Remove cached gpu module so it re-imports with the new pynvml mock."""
    for key in list(sys.modules.keys()):
        if "kpidash_client.telemetry.gpu" in key:
            del sys.modules[key]


def test_collect_gpu_returns_none_when_nvml_init_fails():
    _fresh_gpu_module()
    pynvml_mock = MagicMock()
    pynvml_mock.nvmlInit.side_effect = Exception("No driver")
    pynvml_mock.NVMLError = Exception

    with patch.dict("sys.modules", {"pynvml": pynvml_mock}):
        from kpidash_client.telemetry.gpu import collect_gpu

        result = collect_gpu()

    assert result is None


def test_collect_gpu_returns_none_when_no_devices():
    _fresh_gpu_module()
    pynvml_mock = MagicMock()
    pynvml_mock.nvmlDeviceGetCount.return_value = 0
    pynvml_mock.NVMLError = Exception

    with patch.dict("sys.modules", {"pynvml": pynvml_mock}):
        from kpidash_client.telemetry.gpu import collect_gpu

        result = collect_gpu()

    assert result is None


# ---------------------------------------------------------------------------
# collect_gpu — happy path
# ---------------------------------------------------------------------------


def test_collect_gpu_returns_dict_when_available():
    _fresh_gpu_module()
    pynvml_mock = MagicMock()
    device = MagicMock()
    pynvml_mock.nvmlDeviceGetHandleByIndex.return_value = device
    pynvml_mock.nvmlDeviceGetCount.return_value = 1
    pynvml_mock.nvmlDeviceGetName.return_value = "RTX 4090"
    mem = MagicMock()
    mem.used = 2 * 1024**3
    mem.total = 24 * 1024**3
    pynvml_mock.nvmlDeviceGetMemoryInfo.return_value = mem
    util = MagicMock()
    util.gpu = 75
    pynvml_mock.nvmlDeviceGetUtilizationRates.return_value = util
    pynvml_mock.NVMLError = Exception

    with patch.dict("sys.modules", {"pynvml": pynvml_mock}):
        from kpidash_client.telemetry.gpu import collect_gpu

        result = collect_gpu()

    assert result is not None
    assert result["name"] == "RTX 4090"
    assert result["vram_used_mb"] == 2048
    assert result["vram_total_mb"] == 24576
    assert result["compute_pct"] == 75.0


# ---------------------------------------------------------------------------
# collect_disks
# ---------------------------------------------------------------------------


def test_collect_disks_returns_list():
    from kpidash_client.config import DiskConfig
    from kpidash_client.telemetry.disk import collect_disks

    disk_usage = MagicMock()
    disk_usage.total = 500 * 1024**3  # 500 GB
    disk_usage.used = 200 * 1024**3  # 200 GB

    configs = [DiskConfig(path="/mnt/ssd", label="SSD", type="ssd")]

    with patch("psutil.disk_usage", return_value=disk_usage):
        results = collect_disks(configs)

    assert len(results) == 1
    assert results[0]["label"] == "SSD"
    assert results[0]["total_gb"] > 0
    assert results[0]["used_gb"] > 0


def test_collect_disks_skips_unreadable():
    from kpidash_client.config import DiskConfig
    from kpidash_client.telemetry.disk import collect_disks

    configs = [DiskConfig(path="/nonexistent/path", label="ghost", type="hdd")]

    with patch("psutil.disk_usage", side_effect=OSError("No such file")):
        results = collect_disks(configs)

    assert results == []


def test_collect_disks_type_from_config_on_windows():
    from kpidash_client.config import DiskConfig
    from kpidash_client.telemetry.disk import collect_disks

    disk_usage = MagicMock()
    disk_usage.total = 1 * 1024**3
    disk_usage.used = 0

    configs = [DiskConfig(path="C:\\", label="C", type="ssd")]

    with (
        patch("psutil.disk_usage", return_value=disk_usage),
        patch("platform.system", return_value="Windows"),
    ):
        results = collect_disks(configs)

    assert len(results) == 1
    assert results[0]["type"] == "ssd"
