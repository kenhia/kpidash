# kpidash Development Guidelines

Auto-generated from all feature plans. Last updated: 2026-04-05

## Project Overview

kpidash is a multi-component system:
1. **kpidash** — C11/LVGL fullscreen dashboard on Raspberry Pi 5 (aarch64, DRM/KMS)
2. **kpidash-client** — Python 3.11+ cross-platform daemon + CLI (Linux & Windows)
3. **kpidash-mcp** — Python 3.11+ MCP server for AI agent activity reporting

All components communicate via a Redis server running on the Pi 5.

## Active Technologies
- C11 (dashboard), Python 3.13 (kpidash-client) + LVGL 9.x (embedded GUI), hiredis (Redis C client), libcjson (JSON parsing), psutil/pynvml (Python telemetry) (002-exploration-sprint)
- Redis (all telemetry, health, activities, repos, commands) (002-exploration-sprint)
- C11 + LVGL 9.2.2 (vendored), hiredis, libcjson, libdrm, libpng (003-refinement-sprint)
- Redis 7.x (message bus, not persistent data store) (003-refinement-sprint)

### Dashboard (C / LVGL)
- C11, CMake 3.22+, LVGL 9.2.2 (DRM/KMS backend)
- hiredis v1.2.0 — Redis client
- cJSON, libpng, libdrm, libfreetype (system packages, arm64)
- Cross-compiled: x86_64 → aarch64 via `cmake/aarch64-toolchain.cmake`

### Client & MCP Server (Python)
- Python 3.11+, pyproject.toml packaging
- psutil, pynvml, redis-py, GitPython, click, tomllib (stdlib)
- MCP server: anthropic `mcp` SDK
- Config: TOML at `~/.config/kpidash-client/config.toml`

### Storage
- Redis 7.x on Pi 5, auth via `REDISCLI_AUTH` env var
- Key contract: `specs/001-mvp-dashboard/contracts/redis-schema.md`

## Project Structure

```text
kpidash/
├── CMakeLists.txt             # Dashboard build
├── lv_conf.h                  # LVGL config
├── src/                       # C dashboard source
│   ├── main.c
│   ├── registry.c/h           # Client array (mutex-protected)
│   ├── redis.c/h              # hiredis polling (1s interval)
│   ├── protocol.h             # Redis key constants + limits
│   ├── config.c/h             # Dashboard config
│   ├── status.c/h             # Status message queue
│   └── widgets/               # Per-widget LVGL modules
├── lib/lvgl/                  # LVGL submodule
├── cmake/                     # Toolchain files
├── resources/                 # PNG assets
├── docs/                      # Architecture + protocol docs
├── clients/
│   ├── kpidash-client/        # Python daemon + CLI
│   └── kpidash-mcp/           # Python MCP server
└── specs/001-mvp-dashboard/   # Active feature spec + plan
```

## Commands

### Dashboard build
```bash
# Cross-compile for Pi 5
cmake -B build-pi -DCMAKE_TOOLCHAIN_FILE=cmake/aarch64-toolchain.cmake -DCMAKE_BUILD_TYPE=Release
cmake --build build-pi --parallel

# Native build (dev/test on x86_64)
cmake -B build && cmake --build build --parallel
```

### Client install (dev)
```bash
cd clients/kpidash-client && pip install -e .
cd clients/kpidash-mcp && pip install -e .
```

### Pre-commit checks
```bash
# C dashboard
clang-format --dry-run --Werror src/*.c src/*.h src/widgets/*.c src/widgets/*.h
cppcheck --enable=warning,style,performance --error-exitcode=1 src/
cmake -B build && cmake --build build --parallel && ctest --test-dir build -V

# Python (run from each client package dir)
ruff format --check . && ruff check . && ty check && pytest -q
```

## Code Style

- **C**: clang-format (project `.clang-format` file governs style); `-Wall -Wextra` always on
- **Python**: ruff format + ruff check; type annotations encouraged; `ty check` must pass
- Redis key naming: `kpidash:{category}:{hostname}:{subcategory}` lowercase with colons
- JSON values in Redis use `snake_case` field names

## Key Constraints

- Dashboard has NO mouse/keyboard — no scrollable UI elements anywhere
- Dashboard runs fullscreen with no title bar or window chrome
- Dashboard Redis polling is synchronous 1s cycle; must not stall LVGL render loop
- All Redis passwords via `REDISCLI_AUTH` env var only — never in code or config files
- Client disk types on Windows come from user config (not auto-detected)
- GPU support: NVIDIA only for MVP (pynvml / NVML)

## Recent Changes
- 003-refinement-sprint: Added C11 + LVGL 9.2.2 (vendored), hiredis, libcjson, libdrm, libpng
- 002-exploration-sprint: Added C11 (dashboard), Python 3.13 (kpidash-client) + LVGL 9.x (embedded GUI), hiredis (Redis C client), libcjson (JSON parsing), psutil/pynvml (Python telemetry)

- 001-mvp-dashboard: Full planning complete — Redis schema, data model, Python client + MCP server architecture, widget module breakdown

<!-- MANUAL ADDITIONS START -->
<!-- MANUAL ADDITIONS END -->
