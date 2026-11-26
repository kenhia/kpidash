"""
Dependency guard (WI 4608): NVML comes from `nvidia-ml-py`, never the
deprecated `pynvml` distribution. Both provide the same `import pynvml`, but
the `pynvml` distribution is a shim that prints a FutureWarning at import on
every client start. Pinned here so a later edit can't reintroduce it.
"""

import re
import tomllib
from pathlib import Path

PYPROJECT = Path(__file__).resolve().parent.parent / "pyproject.toml"


def _dist_names() -> set[str]:
    deps = tomllib.loads(PYPROJECT.read_text())["project"]["dependencies"]
    return {re.split(r"[<>=!~\[; ]", d, maxsplit=1)[0].strip().lower() for d in deps}


def test_nvml_comes_from_nvidia_ml_py():
    assert "nvidia-ml-py" in _dist_names()


def test_deprecated_pynvml_distribution_not_declared():
    assert "pynvml" not in _dist_names()
