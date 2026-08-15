# Tasks: Refinement & Standardization

**Input**: Design documents from `/specs/003-refinement-sprint/`
**Prerequisites**: plan.md, spec.md, research.md, data-model.md, quickstart.md

**Tests**: Unit arithmetic tests required by FR-002/SC-007. Widget/GUI code exempt from TDD per constitution.

**Organization**: Tasks grouped by user story for independent implementation and testing.

## Format: `[ID] [P?] [Story] Description`

- **[P]**: Can run in parallel (different files, no dependencies)
- **[Story]**: Which user story this task belongs to (e.g., US1, US2)
- Exact file paths included in descriptions

---

## Phase 1: Setup

**Purpose**: Create new files and shared infrastructure that multiple stories depend on.

- [X] T001 Create `src/layout.h` with unit system constants (SCR_W, SCR_H, UNIT_W=632, UNIT_H=628, CELL_PAD=4, PAD_TOP, PAD_LEFT=24, PAD_RIGHT=24, FOOTER_H=276, COLS, ROWS), simplified macros (UNIT_W_N(n)=n*UNIT_W, UNIT_H_N, ROW_Y, COL_X), and _Static_assert pixel accounting per data-model.md. Widgets apply CELL_PAD as internal padding for 8px visual gap between adjacent content.
- [X] T002 [P] Create `src/widgets/common.h` with WS_COLOR_* palette (12 colors) and WS_FONT_* pointers (5 fonts) per data-model.md §2
- [X] T003 [P] Create `tests/test_layout.c` with unit arithmetic tests: verify UNIT_W_N(1)=632, UNIT_W_N(2)=1264, UNIT_W_N(6)=3792, UNIT_H_N(1)=628, UNIT_H_N(3)=1884, ROW_Y(0)=0, ROW_Y(1)=628, ROW_Y(2)=1256, COL_X(0)=24, COL_X(5)=3184, horizontal sum==3840, vertical sum==2160, content width=UNIT_W-2*CELL_PAD==624, content height=UNIT_H-2*CELL_PAD==620
- [X] T004 Register `tests/test_layout.c` in `tests/CMakeLists.txt` and verify all tests pass via `ctest`

**Checkpoint**: `layout.h`, `common.h`, and `test_layout` exist; `ctest` passes with new layout tests + all 133 existing C tests.

---

## Phase 2: Foundational — Unit System Adoption (Blocking)

**Purpose**: Replace magic numbers in `ui.c` with unit macros. MUST complete before US2 (grid needs layout.h in use).

**⚠️ CRITICAL**: US2 depends on this phase; US3/US4/US5 do not.

- [X] T005 [US1] Refactor `src/ui.c` `ui_init()`: replace `pad_all(8)` → PAD_LEFT/PAD_TOP positioning; replace `card_area_h=640` → UNIT_H; replace `scr_w - 16` → `UNIT_W_N(COLS)`; use ROW_Y/COL_X for all widget positions; each widget applies CELL_PAD as internal padding via `lv_obj_set_style_pad_all(obj, CELL_PAD, 0)`
- [X] T006 [US1] Refactor `src/ui.c` widget sizing: Activities `1256,620` → `UNIT_W_N(2), UNIT_H`; Repo Status `1256,620` → `UNIT_W_N(2), UNIT_H`; card grid width → `UNIT_W_N(COLS)`; client cards → `UNIT_W, UNIT_H`; fortune/status bar sizes using FOOTER_H and layout constants. Note: LVGL objects are now cell-sized (gap-inclusive); content is inset by CELL_PAD
- [X] T007 [US1] Refactor `src/ui.c` widget positioning: card grid at `COL_X(0), ROW_Y(0)`; activities at `COL_X(0), ROW_Y(1)`; repo status at `COL_X(2), ROW_Y(1)`; fortune/status bar positioned relative to ROW_Y(ROWS) in footer area
- [X] T008 [US1] Verify zero raw pixel literals remain for layout in `src/ui.c` — grep for `632`, `628`, `1264`, `624`, `620`, `1256`, `640`, `1272` and confirm no hits in sizing/positioning code (SC-001)
- [X] T009 [US1] Cross-compile and deploy to Pi: verify 3 rows of unit-height content render without clipping, footer accommodates fortune + status bar (SC-001b)

**Checkpoint**: `ui.c` uses only unit macros. Dashboard renders identically to Sprint 002 on 3840×2160 but with formalized layout. All 3 unit rows available.

---

## Phase 3: User Story 2 — Unit-Aware Dev Grid (Priority: P2)

**Goal**: Dev grid overlay draws lines at unit boundaries for visual verification.

**Independent Test**: `redis-cli SET kpidash:cmd:grid '{"enabled":true,"unit":true,"size":1}'` → grid lines at unit boundaries. `"size":0.5` → half-unit. No `"unit"` field → pixel-based (unchanged).

**Depends on**: Phase 2 (layout.h constants must be in use)

- [X] T010 [US2] Extend grid command JSON parsing in `src/redis.c` to read `"unit"` (bool) and `"size"` as float when unit mode active; update `grid_cmd_t` struct in `src/protocol.h` with `unit` and `unit_size` fields per data-model.md §4. Note: map JSON `"size"` → `unit_size` when `unit=true`, else → `size` (pixel mode)
- [X] T011 [US2] Modify `src/widgets/dev_grid.c`: when `unit=true`, draw lines at interior cell boundaries: vertical at `PAD_LEFT + N * unit_size * UNIT_W` (skip screen edges), horizontal at `PAD_TOP + N * unit_size * UNIT_H` (skip screen edges); include `layout.h`; these lines fall at gap midpoints between adjacent widget content; preserve existing pixel-based mode when `unit` field absent
- [X] T012 [US2] Handle edge case: `"unit":true, "size":0` → treat as disabled or clamp to minimum 0.5 units
- [X] T013 [US2] Deploy to Pi: visually verify grid at sizes 0.5, 1, and 2 against known widget boundaries (SC-002)

**Checkpoint**: Dev grid supports both pixel-based and unit-based modes. Pixel mode unchanged from Sprint 002.

---

## Phase 4: User Story 3 — Shared Widget Styles (Priority: P3)

**Goal**: Extract duplicated color/font definitions into `widgets/common.h`, eliminating per-widget `#define` duplication.

**Independent Test**: Build succeeds. `grep -rn 'COLOR_BG\|COLOR_FG\|COLOR_MUTED\|COLOR_HEADER' src/widgets/*.c` shows only `#include "common.h"` usage, no local `#define`.

**Depends on**: Phase 1 (common.h must exist)

- [X] T014 [P] [US3] Migrate `src/widgets/activities.c`: remove local COLOR_*/FONT_* defines, `#include "common.h"`, replace `COLOR_BG` → `WS_COLOR_BG` etc. throughout
- [X] T015 [P] [US3] Migrate `src/widgets/repo_status.c`: remove local COLOR_*/FONT_* defines, `#include "common.h"`, replace with WS_* prefixed constants
- [X] T016 [P] [US3] Migrate `src/widgets/client_card.c`: remove local COLOR_* defines, `#include "common.h"`, replace with WS_* constants; keep widget-specific metric colors as local defines if not in common palette
- [X] T017 [P] [US3] Migrate `src/widgets/dev_graph.c`: remove local COLOR_* defines, `#include "common.h"`, replace with WS_* constants; keep graph series colors as local defines
- [X] T018 [P] [US3] Migrate `src/widgets/fortune.c`: `#include "common.h"`, keep `0x181825` as local `FORTUNE_BG` override (intentionally different), use WS_* for text color
- [X] T019 [P] [US3] Migrate `src/widgets/status_bar.c`: `#include "common.h"`, replace COLOR_BG/WARN/ERROR with WS_* constants
- [X] T020 [P] [US3] Migrate `src/widgets/dev_textsize.c`: `#include "common.h"`, replace any local color/font defines with WS_* constants
- [X] T021 [US3] Verify zero per-widget COLOR_BG/COLOR_FG/COLOR_MUTED/COLOR_HEADER `#define` remain across all widget .c files (SC-003)

**Checkpoint**: All widget files use `common.h`. Single color change in `common.h` propagates to all widgets. Build clean.

---

## Phase 5: User Story 4 — Widget Rebuild Flash Fixes (Priority: P3)

**Goal**: Activities widget stops flashing by skipping rebuild when data unchanged.

**Independent Test**: Watch dashboard 60 seconds with stable activity data — zero visible flashes (SC-005). Repo Status already cached (verify only).

**Depends on**: None (independent of other user stories)

- [X] T022 [US4] Add change detection cache to `src/widgets/activities.c`: static `s_cache[MAX_ACTIVITIES]` + `s_cache_count`, `memcmp` incoming data against cache before rebuilding; skip rebuild if unchanged while elapsed timer continues independently. Note: if US3 (T014) has already migrated this file to `common.h`, work on top of that; if running before T014, coordinate to avoid merge conflict
- [X] T023 [US4] Verify `src/widgets/repo_status.c` existing cache still works correctly under new layout (no code changes expected — confirm `memcmp` + `s_cache` pattern is functional)
- [X] T024 [US4] Deploy to Pi: observe Activities and Repo Status for 60 seconds each with stable data — zero flashes (SC-004, SC-005)

**Checkpoint**: Both widgets update only when data actually changes. No visual flashing.

---

## Phase 6: User Story 5 — Client Card Ordering (Priority: P4)

**Goal**: Client cards render in priority order based on `KPIDASH_PRIORITY_CLIENTS` env var.

**Independent Test**: Set `KPIDASH_PRIORITY_CLIENTS=kubs0,rpi53`, restart dashboard. `kubs0` appears first, `rpi53` second regardless of registration order (SC-008).

**Depends on**: None (independent of other user stories)

- [X] T025 [US5] Add priority sort to `src/ui.c` `ui_refresh()`: after `registry_snapshot()`, sort the `clients[]` array putting priority clients first (in config order), then remaining clients in registration order
- [X] T026 [US5] Handle card reordering in LVGL: when sort order changes (priority client comes online), reorder existing card objects in `g_card_grid` via `lv_obj_move_to_index()` or rebuild card array order
- [X] T027 [US5] Handle edge cases: priority hostname not yet online (skip, no gap); priority hostname never appears (ignore); empty priority list (registration order preserved)
- [X] T028 [US5] Deploy to Pi: verify ordering with `KPIDASH_PRIORITY_CLIENTS=kubs0,rpi53` — priority clients appear first within 1 poll cycle (SC-008)

**Checkpoint**: Card display order respects priority configuration. Existing behavior preserved when env var unset.

---

## Phase 7: Polish & Cross-Cutting Concerns

**Purpose**: Documentation, code quality, and final verification.

- [X] T029 [P] Update `docs/ARCHITECTURE.md`: document unit system constants, 3-row grid layout, FOOTER_H, macro reference (FR-011)
- [X] T030 [P] Update `docs/CLIENT-PROTOCOL.md`: document dev grid `"unit"` boolean field and unit-based `"size"` semantics (FR-012)
- [X] T031 Run `clang-format -i src/*.c src/*.h src/widgets/*.c src/widgets/*.h` and verify clean
- [X] T032 Run `cppcheck --enable=warning,style,performance --error-exitcode=1 src/` — zero warnings
- [X] T033 Run full test suite: `ctest` (133+ C tests) + `pytest` (52 Python tests) — all pass (SC-006)
- [X] T034 Cross-compile and deploy: full validation on Pi 3840×2160 — 3 rows render, grid works, no flashes, cards ordered

---

## Dependencies & Execution Order

### Phase Dependencies

- **Phase 1 (Setup)**: No dependencies — start immediately
- **Phase 2 (Foundational)**: Depends on T001 (layout.h) — BLOCKS Phase 3 (US2)
- **Phase 3 (US2 Grid)**: Depends on Phase 2 completion (layout constants in ui.c)
- **Phase 4 (US3 Styles)**: Depends on T002 (common.h) only — can start after Phase 1
- **Phase 5 (US4 Flash)**: No dependencies on other phases — can start after Phase 1
- **Phase 6 (US5 Ordering)**: Depends on Phase 2 completion (both modify `ui.c`)
- **Phase 7 (Polish)**: Depends on all desired user stories being complete

### User Story Dependencies

```
Phase 1 (Setup) ─┬─► Phase 2 (US1 Foundation) ──► Phase 3 (US2 Grid)
                  │
                  ├─► Phase 4 (US3 Styles) [after T002]
                  │
                  └─► Phase 5 (US4 Flash) [after Phase 1]

Phase 1 (Setup) ──► Phase 2 (US1 Foundation) ──► Phase 6 (US5 Ordering) [both modify ui.c]

All ──► Phase 7 (Polish)
```

### Parallel Execution Opportunities

**Within Phase 1**: T002 + T003 parallelizable (different files)
**Within Phase 4**: T014–T020 all parallelizable (each modifies a different widget file)
**Across stories**: Phase 4 (US3) + Phase 5 (US4) + Phase 6 (US5) can all run in parallel after Phase 1
**Phase 7**: T029 + T030 parallelizable (different docs)

### Implementation Strategy

**MVP (User Story 1 only)**: Phases 1–2 → 9 tasks → unit system formalized, dashboard works
**Incremental**: Add US2 (grid) → US3 (styles) → US4 (flash) → US5 (ordering) as time permits
**Recommended order**: Phase 1 → Phase 2 → Phase 4 (US3, easy wins) → Phase 5 (US4) → Phase 3 (US2) → Phase 6 (US5) → Phase 7
