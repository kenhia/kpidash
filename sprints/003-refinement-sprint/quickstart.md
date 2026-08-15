# Quickstart: Sprint 003 — Refinement & Standardization

## Prerequisites

- C11 compiler (gcc/clang)
- CMake 3.16+
- LVGL 9.2.2 (vendored at `lib/lvgl/`)
- hiredis, libcjson, libdrm, libpng
- Redis 7.x accessible (default: localhost:6379)
- aarch64 cross-compiler for Pi deployment (optional for dev)

## Build & Test (native, test-only)

```bash
cmake -B build -DTESTS_ONLY=ON
cmake --build build --parallel
ctest --test-dir build -V
```

## Build (full, with display)

```bash
cmake -B build
cmake --build build --parallel
```

## Cross-compile & Deploy (Pi 5)

```bash
cmake -B build-pi -DCMAKE_TOOLCHAIN_FILE=cmake/aarch64-toolchain.cmake
cmake --build build-pi --parallel
cmake --build build-pi --target deploy
```

## Key Files for This Sprint

| File | Purpose |
|------|---------|
| `src/layout.h` | NEW — Cell-based unit system constants and macros (UNIT_W=632, CELL_PAD=4) |
| `src/widgets/common.h` | NEW — Shared colors, fonts, style constants |
| `src/ui.c` | MODIFY — Replace magic numbers with unit macros |
| `src/widgets/activities.c` | MODIFY — Add change detection cache |
| `src/widgets/dev_grid.c` | MODIFY — Add unit-aware grid mode |
| `tests/test_layout.c` | NEW — Unit arithmetic macro tests |

## Verification Checklist

```bash
# Format check
clang-format --dry-run --Werror src/*.c src/*.h src/widgets/*.c src/widgets/*.h

# Static analysis
cppcheck --enable=warning,style,performance --error-exitcode=1 src/

# Native build + tests
cmake -B build -DTESTS_ONLY=ON && cmake --build build --parallel && ctest --test-dir build -V

# Cross-compile
cmake -B build-pi -DCMAKE_TOOLCHAIN_FILE=cmake/aarch64-toolchain.cmake
cmake --build build-pi --parallel

# Verify no raw pixel literals remain in ui.c
grep -nE '\b(632|628|1264|624|620|1256)\b' src/ui.c  # Should return zero matches
```

## Redis Dev Grid Commands

```bash
# Existing pixel grid
redis-cli SET kpidash:cmd:grid '{"enabled":true,"size":100}'

# NEW: Unit-based grid (1 unit intervals)
redis-cli SET kpidash:cmd:grid '{"enabled":true,"unit":true,"size":1}'

# NEW: Half-unit intervals
redis-cli SET kpidash:cmd:grid '{"enabled":true,"unit":true,"size":0.5}'

# NEW: Double-unit intervals
redis-cli SET kpidash:cmd:grid '{"enabled":true,"unit":true,"size":2}'
```
