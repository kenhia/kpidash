_default:
    @just --list

# TESTS_ONLY=ON skips the dashboard binary and its LVGL/DRM deps (see
# CMakeLists.txt), so this runs on any x86_64 dev host with no Pi, no sysroot
# and no submodule checkout. The Pi build is cross-compiled and cannot be
# gated here — see CLAUDE.md for that path.

# Gate: everything in the repo
check: check-dashboard check-client

# Gate: configure, build and run the native unit tests
check-dashboard:
    #!/usr/bin/env bash
    set -euo pipefail
    cmake -S . -B build-tests -DTESTS_ONLY=ON
    cmake --build build-tests
    ctest --test-dir build-tests --output-on-failure

# Gate: the Python client (lint + tests)
check-client:
    cd clients/kpidash-client && uv run --extra dev ruff check .
    cd clients/kpidash-client && uv run --extra dev pytest -q

# Build and publish kpidash-client to the homelab package store
# (see k-homelab docs/deploying.md). kpkg refuses an already-published
# version — bump clients/kpidash-client/pyproject.toml first.
publish:
    #!/usr/bin/env bash
    set -euo pipefail
    cd clients/kpidash-client
    rm -rf dist && uv build
    d=$(ssh -n kubsdb mktemp -d)
    scp dist/* kubsdb:"$d"/
    ssh -n kubsdb "kpkg add $d/* && rm -rf $d"
