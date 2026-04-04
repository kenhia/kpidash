# Tasks: Exploration Sprint

**Input**: Design documents from `/specs/002-exploration-sprint/`
**Prerequisites**: plan.md (required), spec.md (required), research.md, data-model.md, contracts/redis-schema.md, quickstart.md

**Tests**: Included for testable C-side logic (JSON parsing, value clamping). GUI rendering is validated on-device per constitution.

**Organization**: Tasks grouped by user story for independent implementation and testing.

## Format: `[ID] [P?] [Story] Description`

- **[P]**: Can run in parallel (different files, no dependencies)
- **[Story]**: Which user story (US1, US2, US3, US4)
- Exact file paths included in descriptions

---

## Phase 1: Setup

**Purpose**: Branch creation and shared infrastructure changes needed by multiple stories

- [x] T001 Create and checkout branch `002-exploration-sprint` from `main`
- [x] T002 Add `os_name` field (`char os_name[128]`) to `client_info_t` in `src/registry.h`
- [x] T003 [P] Add command key macros (`KPIDASH_KEY_CMD_GRID`, `KPIDASH_KEY_CMD_TEXTSIZE`, `CMD_TTL_S`) to `src/protocol.h`
- [x] T004 [P] Add `dev_cmd_state_t` struct to `src/registry.h` and global instance accessor
- [x] T005 Parse `os_name` from health JSON in `redis_parse_health_json()` in `src/redis.c` (optional field, default empty string)
- [x] T006 [P] Add unit test for `os_name` parsing in `tests/test_redis_json.c` — test present field, absent field (backward compat), empty string

**Checkpoint**: `os_name` flows from Redis → registry. Command key macros defined. Branch ready for story work.

---

## Phase 2: Foundational (Blocking Prerequisites)

**Purpose**: Python client changes that feed data to the dashboard; must deploy before visual work

**⚠️ CRITICAL**: Client must be updated and restarted before US1 widget can display OS name

- [x] T007 Collect OS name via `platform.system() + " " + platform.release()` in `clients/kpidash-client/kpidash_client/telemetry/system.py`
- [x] T008 Include `os_name` in health JSON payload in `clients/kpidash-client/kpidash_client/redis_client.py` `write_health()` method
- [x] T009 [P] Add Python test for `os_name` collection in `clients/kpidash-client/tests/test_telemetry.py`
- [x] T010 [P] Add Python test for `os_name` in health JSON payload in `clients/kpidash-client/tests/test_redis_client.py`
- [x] T011 Restart daemon on all clients to deploy os_name change (`uv run kpidash-client daemon stop && uv run kpidash-client daemon start`)

**Checkpoint**: All clients report `os_name` in health JSON. Dashboard parses it into `client_info_t.os_name`.

---

## Phase 3: User Story 1 — Client Widget Redesign (Priority: P1) 🎯 MVP

**Goal**: Replace text-based client cards with compact graphical widgets using arc gauges, health circle, text boxes, hostname/OS bar, and disk bars.

**Independent Test**: Deploy dashboard with ≥2 clients; visually confirm arc gauges, hostname, OS, uptime, disk bars render correctly per reported telemetry. Test with GPU and no-GPU clients.

### Implementation for User Story 1

- [x] T012 [US1] Rewrite `client_card_create()` in `src/widgets/client_card.c` — create compound widget container with arc gauge area, text boxes, hostname bar, and disk bar container. Set container size for ~300px width (6 across at 1920). Remove scrolling flag.
- [x] T013 [US1] Implement center health circle in `src/widgets/client_card.c` — `lv_obj` with round border-radius, green fill when online, red when offline. Position at center of arc area.
- [x] T014 [US1] Implement GPU arcs (left side) in `src/widgets/client_card.c` — two `lv_arc` objects: outer (VRAM %) and inner (GPU usage %). Background angles for lower-left hemisphere (trim 5° from ends for gap per R1). `lv_arc_set_range(0, 100)`, display-only (`LV_OBJ_FLAG_CLICKABLE` removed). Arc width per R2 (outer 7px, inner 7px, 2px gap).
- [x] T015 [US1] Implement RAM/CPU arcs (right side) in `src/widgets/client_card.c` — three `lv_arc` objects: outer (RAM %), inner-bottom (CPU top-core drawn first), inner-top (CPU avg drawn second, overlays top-core). Background angles for lower-right hemisphere (trim 5° from ends). Same sizing as GPU arcs.
- [x] T016 [P] [US1] Implement GPU text box in `src/widgets/client_card.c` — `lv_label` positioned left of arc area showing "VRAM: X MB / Y GB" line 1, "Usage: Z%" line 2. Hidden when `gpu.present == false`.
- [x] T017 [P] [US1] Implement CPU/RAM text box in `src/widgets/client_card.c` — `lv_label` positioned right of arc area showing "RAM: X GB / Y GB" line 1, "CPU: avg% / top%" line 2.
- [x] T018 [US1] Implement no-GPU fallback in `src/widgets/client_card.c` — when `gpu.present == false`, set left arcs to solid gray (`LV_STYLE_ARC_COLOR` on both `LV_PART_MAIN` and `LV_PART_INDICATOR`), hide GPU text label.
- [x] T019 [US1] Implement hostname bar in `src/widgets/client_card.c` — full-width container below arcs: bold hostname label (large font), smaller OS name label below (`client_info_t.os_name`). Truncate hostname with ellipsis if >20 chars via `lv_label_set_long_mode(LV_LABEL_LONG_DOT)`.
- [x] T020 [US1] Implement uptime label in `src/widgets/client_card.c` — positioned upper-right of hostname region. Format reuses existing `format_uptime()` helper ("up Xd Yh").
- [x] T021 [US1] Implement disk bars in `src/widgets/client_card.c` — container below hostname bar holding up to 6 disk bar rows. Each bar: `lv_bar` with fill color based on usage tier (green ≤60%, orange 60–75%, red >75%). Three `lv_label` children: used amount left-justified, total amount right-justified, drive label centered.
- [x] T022 [US1] Update `client_card_update_health()` in `src/widgets/client_card.c` — set center circle color (green/red), update uptime label, update OS name label.
- [x] T023 [US1] Update `client_card_update_telemetry()` in `src/widgets/client_card.c` — set arc values for all 5 arcs (GPU VRAM, GPU usage, RAM, CPU top-core, CPU avg) via `lv_arc_set_value()`. Update GPU text box, CPU/RAM text box. Update disk bars (create/remove as `disk_count` changes, cap at 6). Clamp all percentage values to 0–100.
- [x] T024 [US1] Adjust top-row layout in `src/ui.c` — modify `g_card_grid` flex container sizing for ~320px per card (6 across at 1920px). Adjust card grid height to accommodate new widget height (~280px).

**Checkpoint**: Client widgets display arc gauges, health circle, hostname/OS, uptime, and disk bars. 6 widgets fit across the top row at 1920×1080.

---

## Phase 4: User Story 2 — Development Commands (Priority: P2)

**Goal**: Enable grid overlay and text size reference widget toggled via Redis commands.

**Independent Test**: With dashboard running, `redis-cli SET kpidash:cmd:grid '{"enabled":true,"size":50}' EX 300` → grid appears. `redis-cli SET kpidash:cmd:textsize '{"enabled":true}' EX 300` → text size widget appears.

### Implementation for User Story 2

- [x] T025 [US2] Add command key polling to `redis_poll()` in `src/redis.c` — `GET kpidash:cmd:grid` and `GET kpidash:cmd:textsize` each poll cycle. Parse JSON, update `dev_cmd_state_t` global. Handle absent keys (treat as disabled).
- [x] T026 [P] [US2] Add unit test for grid command JSON parsing in `tests/test_redis_json.c` — test `{"enabled":true,"size":50}`, `{"enabled":false}`, absent key, invalid JSON, size ≤ 0.
- [x] T027 [P] [US2] Add unit test for textsize command JSON parsing in `tests/test_redis_json.c` — test `{"enabled":true}`, `{"enabled":false}`, absent key.
- [x] T028 [US2] Create grid overlay widget in `src/widgets/dev_grid.c` and `src/widgets/dev_grid.h` — full-screen `lv_obj` at high z-index with translucent line objects spaced at `grid_size` pixels (horizontal + vertical). Expose `dev_grid_create(lv_obj_t *parent, int grid_size)` and `dev_grid_destroy()`.
- [x] T029 [US2] Create text size reference widget in `src/widgets/dev_textsize.c` and `src/widgets/dev_textsize.h` — panel showing sample text at sizes 10, 12, 14, 16, 18, 20, 24, 28, 32px. Expose `dev_textsize_create(lv_obj_t *parent)` and `dev_textsize_destroy()`.
- [x] T030 [US2] Wire command state to UI in `src/ui.c` — in `ui_refresh()`, check `dev_cmd_state_t`: create/destroy grid overlay when `grid_enabled` changes; create/destroy textsize widget when `textsize_enabled` changes. Handle re-creation on grid size change.
- [x] T031 [US2] Add new widget source files to `CMakeLists.txt` — add `src/widgets/dev_grid.c` and `src/widgets/dev_textsize.c` to the kpidash target sources.

**Checkpoint**: Grid overlay and text size reference toggle via Redis commands. Auto-disable on TTL expiry.

---

## Phase 5: User Story 3 — Widget Tuning (Priority: P3, Exploratory)

**Goal**: Reduce Activities and Repo Status widget footprint, increase text size, dim repo host prefix, improve row alignment.

**Independent Test**: Visual comparison before/after on the Pi display at 1–2m distance. Use grid overlay (US2) to verify sizing.

### Implementation for User Story 3

- [ ] T032 [US3] Tune Activities widget sizing in `src/widgets/activities.c` — reduce container width, increase font size for activity names and elapsed times. Ensure each row's elapsed time visually aligns with its activity name (consider tabular layout or right-aligned column).
- [ ] T033 [US3] Tune Repo Status widget sizing in `src/widgets/repo_status.c` — reduce container width, increase font size for repo names and status indicators.
- [ ] T034 [US3] Implement host prefix dimming in `src/widgets/repo_status.c` — use `lv_label_set_recolor(label, true)` and format repo entries as `#888888 host/#repo` so "host/" appears dimmer than "repo". Alternatively, split into two labels with different opacity.
- [ ] T035 [US3] Cap visible entries in `src/widgets/activities.c` at `ACTIVITY_MAX_DISPLAY` and in `src/widgets/repo_status.c` at a reasonable max (e.g., 15). Hide overflow without scrolling.
- [ ] T036 [US3] Adjust middle-row layout in `src/ui.c` — resize Activities and Repo Status containers to fit the reduced footprint. Rebalance left/right split if needed.

**Checkpoint**: Activities and Repo Status have larger text, less wasted space, and dimmed host prefixes. Row data alignment improved.

---

## Phase 6: User Story 4 — Quasi-Realtime Graphs (Priority: P4, Exploratory)

**Goal**: Explore time-series GPU/VRAM graph display using `lv_chart`. Determine feasibility and layout integration.

**Independent Test**: With a GPU-equipped client, enable graph display and confirm GPU usage and VRAM charts update in near-real-time.

### Implementation for User Story 4

- [ ] T037 [US4] Create GPU graph widget in `src/widgets/gpu_graph.c` and `src/widgets/gpu_graph.h` — two `lv_chart` objects (`LV_CHART_TYPE_LINE`, `LV_CHART_UPDATE_MODE_SHIFT`): one for GPU compute % (range 0–100), one for VRAM used MB (range 0–max_vram). Point count ~300 (5 min at ~1/s). Expose `gpu_graph_create(lv_obj_t *parent)`, `gpu_graph_update(float compute_pct, uint32_t vram_mb)`, `gpu_graph_destroy()`.
- [ ] T038 [US4] Add graph toggle mechanism — reuse dev command pattern: add `kpidash:cmd:graph` Redis key. Add macro to `src/protocol.h`, parse in `redis_poll()` in `src/redis.c`, add `graph_enabled` + `graph_client` fields to `dev_cmd_state_t`.
- [ ] T039 [US4] Wire graph widget to UI in `src/ui.c` — when graph enabled, create graph widget in available screen area. Feed data from the specified client's telemetry on each `ui_refresh()`. When graph active and 6+ clients, suppress the two lowest-priority client widgets (reuse `KPIDASH_PRIORITY_CLIENTS` ordering).
- [ ] T040 [US4] Add `src/widgets/gpu_graph.c` to `CMakeLists.txt` target sources.

**Checkpoint**: GPU/VRAM graphs render and update. Layout accommodates graphs by suppressing low-priority clients.

---

## Phase 7: Polish & Cross-Cutting Concerns

**Purpose**: Documentation, code quality, and validation across all stories

- [ ] T041 [P] Update `docs/CLIENT-PROTOCOL.md` — add `os_name` field to health JSON schema, add `kpidash:cmd:grid`, `kpidash:cmd:textsize`, and `kpidash:cmd:graph` key documentation
- [ ] T042 [P] Update `docs/ARCHITECTURE.md` — update widget layout diagram to reflect new arc-gauge client cards, dev overlay, and optional graph area
- [ ] T043 Run `clang-format -i src/*.c src/*.h src/widgets/*.c src/widgets/*.h` and `cppcheck` across all modified files
- [ ] T044 Run `ruff format . && ruff check .` in `clients/kpidash-client/`
- [ ] T045 Run full test suite: `ctest --test-dir build-tests -V` (C) and `pytest -q` (Python)
- [ ] T046 Cross-compile, deploy to Pi, and perform on-device visual validation of all stories per quickstart.md

**Checkpoint**: All code formatted, linted, tested. Dashboard renders correctly on Pi at 1920×1080.

---

## Dependencies & Execution Order

### Phase Dependencies

- **Setup (Phase 1)**: No dependencies — start immediately
- **Foundational (Phase 2)**: Depends on T002 (os_name in registry.h) and T005 (parse in redis.c) from Setup
- **US1 Client Widget (Phase 3)**: Depends on Phase 1 + Phase 2 complete (os_name must flow end-to-end)
- **US2 Dev Commands (Phase 4)**: Depends on Phase 1 (T003, T004 — macros and state struct). Independent of US1.
- **US3 Widget Tuning (Phase 5)**: Depends on Phase 1 only. Benefits from US2 (grid overlay) but not blocked by it.
- **US4 Graphs (Phase 6)**: Depends on Phase 1 (T003). Independent of US1/US2/US3.
- **Polish (Phase 7)**: Depends on all desired story phases being complete.

### User Story Dependencies

- **US1 (P1)**: Blocked by Phase 2 (client must report os_name). This is the MVP.
- **US2 (P2)**: Independent of US1 — can run in parallel after Phase 1
- **US3 (P3)**: Independent of US1/US2 — can start after Phase 1. Grid overlay (US2) is helpful but not blocking.
- **US4 (P4)**: Independent of US1/US2/US3 — can start after Phase 1.

### Within Each User Story

- Container layout before child widgets
- Arc creation before arc value updates
- Widget structure before update functions
- All implementation before checkpoint validation

### Parallel Opportunities

**After Phase 1 completes, these can proceed in parallel:**
- Phase 2 (Python client) + Phase 4 (US2 Dev Commands) + Phase 5 (US3 Widget Tuning)
- Phase 3 (US1 Client Widget) starts after Phase 2 completes
- Within US1: T016 (GPU text) and T017 (CPU text) are parallel

---

## Parallel Example: User Story 1

```bash
# After Phase 2 completes:

# Parallel batch 1 - widget structure
Task T012: Rewrite client_card_create() container
Task T013: Center health circle

# Sequential - arcs depend on container
Task T014: GPU arcs (left)
Task T015: RAM/CPU arcs (right)

# Parallel batch 2 - text boxes (independent of each other)
Task T016: GPU text box
Task T017: CPU/RAM text box

# Sequential - depends on arcs
Task T018: No-GPU fallback

# Parallel batch 3 - lower sections
Task T019: Hostname bar
Task T020: Uptime label
Task T021: Disk bars

# Sequential - update functions depend on all structure
Task T022: Update health function
Task T023: Update telemetry function
Task T024: Layout adjustment
```

---

## Implementation Strategy

### MVP First (User Story 1 Only)

1. Complete Phase 1: Setup
2. Complete Phase 2: Foundational (Python client os_name)
3. Complete Phase 3: User Story 1 (Client Widget Redesign)
4. **STOP and VALIDATE**: Deploy to Pi, verify arc gauges render, 6 widgets fit
5. This is the sprint MVP — all other stories are incremental

### Incremental Delivery

1. Setup + Foundational → os_name flowing
2. US1 → Arc gauge widgets live on Pi (MVP!)
3. US2 → Grid overlay available for iteration
4. US3 → Activities/Repo widgets tuned using grid overlay
5. US4 → GPU graphs available (exploratory)
6. Polish → Docs, formatting, full test pass

### Parallel Opportunity

After Phase 1 completes:
- **Track A**: Phase 2 → Phase 3 (US1: client widget — needs os_name from Phase 2)
- **Track B**: Phase 4 (US2: dev commands — independent)
- **Track C**: Phase 5 (US3: widget tuning — independent)
- **Track D**: Phase 6 (US4: graphs — independent)

---

## Notes

- [P] tasks = different files, no dependencies on incomplete tasks in same phase
- [USn] label maps task to specific user story
- Constitution gate: clang-format + cppcheck + ruff before any commit
- GUI tasks need on-device validation (per constitution III exemption)
- Exploratory stories (US3, US4) may generate spec updates during iteration
- Commit after each task or logical group
