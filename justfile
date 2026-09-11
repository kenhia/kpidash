_default:
    @just --list

# TESTS_ONLY=ON skips the dashboard binary and its LVGL/DRM deps (see
# CMakeLists.txt), so this runs on any x86_64 dev host with no Pi and no
# sysroot. The Pi build is cross-compiled and cannot be gated here — see
# CLAUDE.md for that path.
#
# WI #2244 added ONE submodule the gate needs: lib/kdashdata, whose kdash_core
# carries the apartment-temperature band classifier that src/registry.c calls.
# `_kdash` below initialises it, so a clean clone still passes in one command.
# lib/lvgl is deliberately NOT initialised — it is the big one, and a
# tests-only build has never needed it.

# Gate: everything in the repo
check: check-dashboard check-release check-client

# Init just the one submodule the tests need (no-op once present).
[private]
_kdash:
    #!/usr/bin/env bash
    set -euo pipefail
    if [ ! -f lib/kdashdata/CMakeLists.txt ]; then
        git submodule update --init lib/kdashdata
    fi

# Gate: configure, build and run the native unit tests
check-dashboard: _kdash
    #!/usr/bin/env bash
    set -euo pipefail
    cmake -S . -B build-tests -DTESTS_ONLY=ON
    cmake --build build-tests
    # --no-tests=error: bare ctest prints "No tests were found!!!" and exits 0,
    # so without this the gate would pass loudest when there is least to check.
    ctest --test-dir build-tests --output-on-failure --no-tests=error

# Gate: the same sources again at -O2, warnings fatal (WI #1934).
#
# scripts/deploy.sh builds -DCMAKE_BUILD_TYPE=Release, and GCC's flow-sensitive
# warnings (-Wstringop-truncation, -Wunused-result, the -Wmaybe-* family) only
# run once the optimiser does. The plain dev configure therefore hides warnings
# that are present in the build that actually reaches the Pi — which is how two
# of them survived unnoticed until sprint 017 read the deploy output.
#
# -Werror is safe to demand here precisely because this pass is scoped to what
# TESTS_ONLY compiles. COVERAGE IS PARTIAL AND DELIBERATELY SO: it covers
# config.c, redis.c, registry.c, icons.c and memstat.c — every source the unit
# tests link — and NOT the dashboard-only sources (main.c, ui.c, fortune.c,
# screenshot.c, src/widgets/*), which need LVGL and libpng and so cannot be
# compiled on a bare dev host. Those stay covered by the deploy's own Release
# build. See CLAUDE.md's note on the warning-clean claim.
check-release: _kdash
    #!/usr/bin/env bash
    set -euo pipefail
    cmake -S . -B build-release-check -DTESTS_ONLY=ON \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_C_FLAGS_RELEASE="-O2 -DNDEBUG -Wall -Wextra -Werror"
    cmake --build build-release-check

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
