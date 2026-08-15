# Quickstart: Move Dev Graph to Row 2

**Feature**: 004-graph-row2

## What Changes

Single file: `src/ui.c` — approximately 15 lines modified, net code reduction.

## Changes Summary

1. **`ui_init`**: Move Activities from `COL_X(0)` to `COL_X(2)` and Repo Status from `COL_X(2)` to `COL_X(4)`
2. **`ui_refresh`** (dev graph create): Change parent from `g_card_grid` to `g_screen`, add `lv_obj_set_pos` call, remove `lv_obj_move_to_index` call
3. **`ui_refresh`** (card reorder): Remove `card_offset` variable and use bare `i` index
4. **`ui.c` header comment**: Update ASCII layout diagram

## Build & Verify

```bash
# Format
clang-format -i src/ui.c

# Native build + test
cmake -B build && cmake --build build --parallel
ctest --test-dir build -V

# Cross-compile for Pi 5
cmake -B build-pi -DCMAKE_TOOLCHAIN_FILE=cmake/aarch64-toolchain.cmake
cmake --build build-pi --parallel

# Deploy and verify on device (manual)
# - All 6 card slots visible in Row 1
# - Dev graph appears at columns 0-1 Row 2 when enabled
# - Activities at columns 2-3, Repo Status at columns 4-5
# - No layout shift in Row 1 when graph toggles
```
