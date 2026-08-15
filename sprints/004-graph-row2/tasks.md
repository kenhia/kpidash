# Tasks: Move Dev Graph to Row 2

**Input**: Design documents from `/specs/004-graph-row2/`
**Prerequisites**: plan.md ✅, spec.md ✅, research.md ✅, data-model.md ✅, quickstart.md ✅

**Tests**: Not requested — GUI-only layout change verified manually on device.

**Organization**: Tasks grouped by user story. All changes target a single file (`src/ui.c`), so parallelism is limited.

## Format: `[ID] [P?] [Story] Description`

- **[P]**: Can run in parallel (different files, no dependencies)
- **[Story]**: Which user story this task belongs to (e.g., US1, US2, US3)
- Include exact file paths in descriptions

---

## Phase 1: Setup

**Purpose**: Branch creation and orientation

- [ ] T001 Create feature branch `004-graph-row2` from main
- [ ] T002 Read current layout code and header comment in src/ui.c to confirm baseline positions

**Checkpoint**: Branch ready, current layout understood

---

## Phase 2: User Story 1 - Full Card Grid in Row 1 (Priority: P1) 🎯 MVP

**Goal**: Row 1 card grid always spans all 6 columns; dev graph is no longer a child of `g_card_grid`.

**Independent Test**: Launch dashboard with dev graph disabled; verify all 6 card slots render in Row 1 at full width.

### Implementation for User Story 1

- [ ] T003 [US1] Change dev graph creation parent from `g_card_grid` to `g_screen` in src/ui.c (ui_refresh or dev graph create path)
- [ ] T004 [US1] Remove `lv_obj_move_to_index` call that placed the dev graph at flex index 0 in `g_card_grid` in src/ui.c
- [ ] T005 [US1] Ensure card grid size is always UNIT_W_N(COLS) × UNIT_H (remove any conditional sizing based on `g_dev_graph`) in src/ui.c

**Checkpoint**: Card grid always occupies full 6-column Row 1 width regardless of dev graph state

---

## Phase 3: User Story 2 - Dev Graph in Row 2 (Priority: P1)

**Goal**: Dev graph renders at columns 0–1 of Row 2 via absolute positioning; Activities and Repo Status shift to columns 2–3 and 4–5.

**Independent Test**: Enable dev graph; verify it appears at COL_X(0), ROW_Y(1) in Row 2, with Activities at COL_X(2) and Repo Status at COL_X(4).

### Implementation for User Story 2

- [ ] T006 [US2] Add `lv_obj_set_pos(g_dev_graph, COL_X(0), ROW_Y(1))` after dev graph creation in src/ui.c
- [ ] T007 [US2] Move Activities initial position from COL_X(0), ROW_Y(1) to COL_X(2), ROW_Y(1) in ui_init in src/ui.c
- [ ] T008 [US2] Move Repo Status initial position from COL_X(2), ROW_Y(1) to COL_X(4), ROW_Y(1) in ui_init in src/ui.c

**Checkpoint**: Row 2 layout correct — dev graph at cols 0–1, Activities at cols 2–3, Repo Status at cols 4–5

---

## Phase 4: User Story 3 - No Card Offset Logic (Priority: P1)

**Goal**: Remove card index offset workaround so cards always reorder starting at index 0.

**Independent Test**: Enable dev graph, connect multiple clients; verify cards reorder at index 0 with no offset applied.

### Implementation for User Story 3

- [ ] T009 [US3] Remove `card_offset = g_dev_graph ? 1 : 0` logic from ui_refresh in src/ui.c
- [ ] T010 [US3] Change `lv_obj_move_to_index` calls for cards to use bare `i` instead of `i + card_offset` in src/ui.c

**Checkpoint**: Card ordering is always 0-based regardless of dev graph state

---

## Phase 5: Polish & Cross-Cutting Concerns

**Purpose**: Documentation, validation, and deployment

- [ ] T011 Update ASCII layout diagram in the header comment of src/ui.c to reflect new Row 1/Row 2 arrangement
- [ ] T012 Run `clang-format -i src/ui.c` and verify no lint issues with cppcheck
- [ ] T013 Native build and ctest: `cmake -B build && cmake --build build --parallel && ctest --test-dir build -V`
- [ ] T014 Cross-compile for Pi 5: `cmake -B build-pi -DCMAKE_TOOLCHAIN_FILE=cmake/aarch64-toolchain.cmake && cmake --build build-pi --parallel`
- [ ] T015 Deploy to device and manually verify quickstart.md scenarios (all 6 cards in Row 1, dev graph in Row 2, no layout shift on toggle)

---

## Dependencies & Execution Order

### Phase Dependencies

- **Setup (Phase 1)**: No dependencies — start immediately
- **US1 (Phase 2)**: Depends on Setup — MVP delivery point
- **US2 (Phase 3)**: Depends on US1 (graph must be reparented before positioning)
- **US3 (Phase 4)**: Depends on US1 (offset removal only makes sense after graph leaves card grid)
- **Polish (Phase 5)**: Depends on all user stories complete

### User Story Dependencies

- **US1 (P1)**: No dependencies on other stories — this is the foundational change
- **US2 (P1)**: Depends on US1 (graph parent changed to `g_screen` first, then position it)
- **US3 (P1)**: Depends on US1 (offset existed because graph was in card grid; remove after reparent)
- **US2 and US3**: Independent of each other — could be done in either order after US1

### Parallel Opportunities

- T007 and T008 (Activities + Repo Status repositioning) are independent edits within the same file — apply together in one pass
- T009 and T010 (offset removal) are logically coupled — apply together in one pass
- After US1 is complete, US2 and US3 can proceed in either order

---

## Implementation Strategy

### MVP First (User Story 1 Only)

1. Complete Phase 1: Setup
2. Complete Phase 2: US1 — card grid reclaims full width
3. **STOP and VALIDATE**: Build and verify cards render correctly
4. Continue to US2 + US3 for full feature

### Sequential Delivery (Recommended — Single File)

1. Setup → US1 (reparent graph) → US2 (position graph + shift widgets) → US3 (remove offset) → Polish
2. Each phase is a logical commit point
3. Total scope: ~15 lines changed in `src/ui.c`
