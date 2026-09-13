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
check: check-dashboard check-release check-client check-units check-scripts

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
[doc("The same sources again at -O2, warnings fatal (partial coverage)")]
check-release: _kdash
    #!/usr/bin/env bash
    set -euo pipefail
    cmake -S . -B build-release-check -DTESTS_ONLY=ON \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_C_FLAGS_RELEASE="-O2 -DNDEBUG -Wall -Wextra -Werror"
    cmake --build build-release-check

# Gate (opt-in): the WHOLE tree at -O2, warnings fatal — including the
# dashboard-only sources check-release cannot reach (WI #2307).
#
# check-release is scoped to TESTS_ONLY, which is five files, and NOT the five
# the two warnings in WI #1934 were actually in. That gate could never have
# caught the warnings it was written in response to. This one compiles
# everything: main.c, ui.c, fortune.c, screenshot.c, src/widgets/*.
#
# Deliberately OUT of `just check`, for two reasons: it needs lib/lvgl, which
# is the big submodule a tests-only build has never wanted, and it needs
# libpng-dev on the host. Run it before a deploy and before shipping C changes.
#
# WHAT IT STILL DOES NOT CATCH: linker warnings, and aarch64-only ones in
# particular. The exec-stack warning fixed in sprint 019 was invisible here —
# native x86_64 links clean while the Pi cross-link warns. Read the deploy
# output too; this gate narrows that duty, it does not retire it.
[doc("Opt-in: the WHOLE tree at -O2, warnings fatal (needs lib/lvgl + libpng)")]
check-warnings-full: _kdash
    #!/usr/bin/env bash
    set -euo pipefail
    if [ ! -f lib/lvgl/CMakeLists.txt ]; then
        git submodule update --init lib/lvgl
    fi
    if ! pkg-config --exists libpng; then
        echo "check-warnings-full needs libpng-dev (apt install libpng-dev)" >&2
        exit 1
    fi
    cmake -S . -B build-warnings-full \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_C_FLAGS_RELEASE="-O2 -DNDEBUG -Wall -Wextra -Werror"
    cmake --build build-warnings-full

# Gate: the systemd units this repo authors read the ONE per-host password
# file and no private copy (sprint 020). Static, and it has to be: the failure
# it catches is a unit reading a stale private copy, which looks identical to a
# healthy one for exactly as long as the two values agree.
[doc("The systemd units read the one per-host password file")]
check-units:
    ./scripts/unit-lint.sh

# Gate: scripts/kpidash-auth.sh, this repo's shell copy of CD-19 -- the
# fallback order the fleet's tools share. The Python client is NOT a CD-19
# consumer (systemd hands it the value); these shell helpers have no unit to do
# that for them, so they are the only copy of the chain in this repo.
[doc("The shell copy of CD-19 (scripts/kpidash-auth.sh)")]
check-scripts:
    ./tests/shell/test_kpidash_auth.sh

# Gate: the Python client (lint + tests)
check-client:
    cd clients/kpidash-client && uv run --extra dev ruff check .
    cd clients/kpidash-client && uv run --extra dev pytest -q

# Build and publish kpidash-client to the homelab package store
# (see k-homelab docs/deploying.md). kpkg refuses an already-published
# version — bump clients/kpidash-client/pyproject.toml first.
[doc("Build and publish kpidash-client to the homelab package store")]
publish:
    #!/usr/bin/env bash
    set -euo pipefail
    cd clients/kpidash-client
    rm -rf dist && uv build
    d=$(ssh -n kubsdb mktemp -d)
    scp dist/* kubsdb:"$d"/
    ssh -n kubsdb "kpkg add $d/* && rm -rf $d"
