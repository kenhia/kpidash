"""telemetry/gpu.py — NVIDIA GPU collection via pynvml (T022)"""

from __future__ import annotations

# pynvml is imported lazily so the module can be imported on machines
# without NVIDIA drivers — collect_gpu() returns None gracefully.
_nvml_available: bool | None = None


def _init_nvml() -> bool:
    global _nvml_available
    if _nvml_available is not None:
        return _nvml_available
    try:
        import pynvml  # noqa: PLC0415

        pynvml.nvmlInit()
        _nvml_available = True
    except Exception:
        _nvml_available = False
    return _nvml_available


def collect_gpu() -> dict | None:
    """
    Collect NVIDIA GPU metrics for device 0.

    Returns a dict with name, vram_used_mb, vram_total_mb, compute_pct,
    or None if NVML is unavailable or no GPU is found.
    """
    if not _init_nvml():
        return None
    try:
        import pynvml  # noqa: PLC0415

        count = pynvml.nvmlDeviceGetCount()
        if count == 0:
            return None
        handle = pynvml.nvmlDeviceGetHandleByIndex(0)
        name = pynvml.nvmlDeviceGetName(handle)
        mem = pynvml.nvmlDeviceGetMemoryInfo(handle)
        util = pynvml.nvmlDeviceGetUtilizationRates(handle)
        return {
            "name": name if isinstance(name, str) else name.decode(),
            "vram_used_mb": int(mem.used // (1024 * 1024)),
            "vram_total_mb": int(mem.total // (1024 * 1024)),
            "compute_pct": float(util.gpu),
        }
    except Exception:
        return None
