---
description: "Task list for sprint 006 — Layout Refresh, Service Status Cards, and Graph Polish"
---

# Tasks: Layout Refresh, Service Status Cards, and Graph Polish

**Input**: Design documents from `/home/ken/src/tools/kpidash/specs/006-layout-refresh-status-cards/`
**Prerequisites**: [plan.md](plan.md), [spec.md](spec.md), [research.md](research.md), [data-model.md](data-model.md), [contracts/](contracts/)

**Tests**: Tests **ARE** in scope for this sprint. The plan's Constitution Check commits to TDD (Red-Green-Refactor) for all logic-heavy units: service-state parser, freshness/colour mapping, icon-registry lookup, host-keyed graph router, rows-2-3 priority placement. GUI-only behaviour (overlay visibility, GPU trace thickness, Fortune font size, mockup visuals) is exempt and validated by smoke tests per the constitution's GUI exemption.

**Organization**: Tasks are grouped by user story to enable independent implementation and testing of each story.

## Format: `[ID] [P?] [Story] Description`

- **[P]**: Can run in parallel (different files, no dependencies)
- **[Story]**: Which user story this task belongs to (US1–US6; setup/foundational/polish carry no story tag)
- All file paths are repository-relative.

---

## Phase 1: Setup (Shared Infrastructure)

**Purpose**: Branch + workspace bookkeeping; no code yet.

- [X] T001 Confirm working branch `006-layout-refresh-status-cards`, native build (`build/`) green from `main`, and cross build (`build-pi/`) green via `cmake/aarch64-toolchain.cmake`. Run `cmake --build build --parallel && ctest --test-dir build` and `cmake --build build-pi --parallel` to establish a clean baseline.
- [X] T002 [P] Add `src/icons.h`, `src/icons.c`, `src/widgets/service_card.h`, `src/widgets/service_card.c`, `src/mockup_main.c`, `tests/test_service_card.c`, `tests/test_icon_registry.c`, `tests/test_graph_router.c`, `tests/test_layout_pool.c` as empty/minimal stubs (header guards, TU compiles, one trivial test each); register them in root `CMakeLists.txt` against the `kpidash` target and in `tests/CMakeLists.txt` as new test executables (mirror `test_widget_leak`); verify `cmake --build build --parallel && ctest --test-dir build` is still green so subsequent tasks land into compiling files.
- [X] T003 [P] Pin the canonical Graph widget cell footprint by inspecting `src/widgets/dev_graph.c` (sprint 004) — record the value as `static const uint8_t WIDGET_CELLS_GRAPH = N;` near the future `widget_kind_t` use site in `src/ui.c` (and the matching constants `WIDGET_CELLS_ACTIVITIES`, `WIDGET_CELLS_REPO_STATUS`, `WIDGET_CELLS_FORTUNE = 2`). T011 and T015 reference these constants by name rather than literal integers; update T011's test cases (see below) to use them. Resolves analyze finding I1.

---

## Phase 2: Foundational (Blocking Prerequisites)

**Purpose**: Shared structs, layout pool engine, and the icon-font pipeline that multiple user stories build on. **No user story work begins until Phase 2 completes.**

**⚠️ CRITICAL**: T004–T010 must complete before any of Phase 3+.

- [X] T004 In `src/registry.h`, declare `widget_kind_t` (`WIDGET_GRAPH`, `WIDGET_ACTIVITIES`, `WIDGET_REPO_STATUS`, `WIDGET_FORTUNE`), `widget_request_t { widget_kind_t kind; uint8_t cells; const void *payload; }`, and the pool placement API: `int layout_pool_place(const widget_request_t *requests, size_t n_requests, widget_request_t *out_placed, size_t out_cap)` returning the count of placed widgets in `out_placed`. Pure-logic; no LVGL include.
- [X] T005 In `src/registry.c`, implement `layout_pool_place()` per [data-model.md §6](data-model.md): stable sort by `kind` ascending; greedy fill of a 12-cell budget; drop-and-continue when a request's `cells` would exceed remaining capacity (allows smaller lower-priority widgets to fit after a larger higher-priority widget was dropped). Capacity constant is 12 (6 cols × 2 rows). FR-002, FR-003, FR-004.
- [ ] T006 In `src/registry.h` add `graph_host_series_t { char host[64]; double last_sample_ts; lv_obj_t *widget; bool stale; }` and a small intrusive collection (fixed-size array up to 8 hosts, struct + count + helpers `graph_host_find_or_create(const char *host)`, `graph_host_touch(const char *host, double ts)`, `graph_host_is_stale(const graph_host_series_t *s, double now)`). Stale threshold = 30.0 s. FR-014, FR-015a.
- [X] T007 [P] In `src/registry.h` add `service_state_t` enum (`UNKNOWN`/`OK`/`UNHEALTHY`/`MAINTENANCE`/`DOWN`) and `service_entry_t` per [data-model.md §2](data-model.md) (name, host, text, icon_index, last_payload_ts, last_valid_state, plus opaque `lv_obj_t *` handles guarded so the struct is testable without LVGL — either always-present `void *` or `#ifdef`'d). Include `service_state_t service_parse_state(const char *s)` declaration and `service_color_t service_color(const service_entry_t *e, double now)` returning a small enum (`GREEN`/`YELLOW`/`BLUE`/`GRAY`/`RED`). FR-019, FR-020, FR-022, FR-022a.
- [X] T008 In `src/registry.c`, implement `service_parse_state()` (returns `UNKNOWN` for NULL/empty/unrecognized) and `service_color()` per the [data-model.md §2 state table](data-model.md) (DOWN sticky-gray regardless of freshness; OK/UNHEALTHY/MAINTENANCE map to colour iff `now - last_payload_ts < 60.0`, otherwise RED; UNKNOWN means the card is not rendered — caller responsibility). FR-019, FR-020.
- [X] T009 [P] In `src/icons.h` declare `icon_entry_t { int index; uint32_t codepoint; const char *label; }`, the `ICON_REGISTRY[]` / `ICON_REGISTRY_COUNT` externs, and the lookup API: `const char *icons_lookup(int index)` (returns NULL on unknown), `uint32_t icons_codepoint(int index)` (returns 0 on unknown). Per [contracts/icon-registry.md](contracts/icon-registry.md).
- [X] T010 [P] In `fonts/generate.sh`, append a generator invocation for `lv_font_icons_56` per [research.md R1](research.md): `lv_font_conv --bpp 4 --size 56 --font fonts/ttf/SymbolsNerdFont-Regular.ttf -r 0xF300-0xF381 -r 0xF1D2-0xF1D3 --format lvgl -o fonts/lv_font_icons_56.c`. Run it; commit the generated `fonts/lv_font_icons_56.c`. Add the generated file to the `kpidash` target source list. Declare `extern lv_font_t lv_font_icons_56;` in `fonts/lv_font_custom.h` (or the equivalent header that already declares the Montserrat fonts).

**Checkpoint**: Foundation ready — Phase 3+ may begin in parallel.

---

## Phase 3: User Story 1 — Operator sees the new layout at a glance (Priority: P1) 🎯 MVP

**Goal**: Row 1 = Client Cards only; rows 2-3 = priority pool (Graph > Activities > Repo Status > Fortune) with drop-on-overflow; footer = Service Status Cards strip (initially empty until US2 populates it).

**Independent Test**: Deploy on rpi53 with at least one client + one graph host + one service reporting. Visually confirm Row 1 has only Client Cards, rows 2-3 contains widgets in documented priority order, footer contains a service card. SC-001.

### Tests for User Story 1

- [X] T011 [P] [US1] In `tests/test_layout_pool.c` add cases per FR-003/FR-004 using the `WIDGET_CELLS_*` constants pinned in T003 (do NOT use literal integers): (a) the full canonical request set fits and returns all placed in priority order; (b) an oversized synthetic Graph(>12) drops itself plus any further requests that won't fit; (c) drop-and-continue: a request whose `cells` exceed remaining capacity is dropped but smaller lower-priority requests that still fit ARE placed; (d) multi-instance Graph hosts are emitted alphabetically by `host` and the stable sort preserves that order. Tests MUST FAIL before T012/T013 implementation lands and PASS after.

### Implementation for User Story 1

- [X] T012 [US1] In `src/ui.c`, route Row 1 placement to Client Cards only (FR-001) and call `layout_pool_place()` to obtain the rows-2-3 widget list, then place each returned `widget_request_t` into the next free cell(s) of the 6×2 pool grid using existing `src/layout.h` macros (`COL_X`, `ROW_Y` with `r ∈ {1,2}`). Remove the prior placement code that mixed widget kinds across rows.
- [X] T013 [US1] In `src/ui.c`, replace the footer-Fortune call with a footer Service-Strip container sized `SCR_W − 2·PAD_LEFT` × `FOOTER_H` at `y = PAD_TOP + ROWS·UNIT_H`. The container is initially empty; US2 fills it. Confirm Fortune no longer appears in the footer at runtime. FR-005, FR-006.
- [X] T014 [US1] Extend `src/widgets/fortune.{c,h}` to expose a 2×1 cell renderer entry point `fortune_create(lv_obj_t *parent, lv_coord_t x, lv_coord_t y)` sized `UNIT_W_N(2) × UNIT_H_N(1)` with the larger fixed font (use existing `lv_font_montserrat_bold_36` or `_48` — pick during T014, no new font). FR-007, FR-008.
- [X] T015 [US1] Wire Fortune, Activities, Repo Status, and (per-host) Graph as `widget_request_t` entries into the `ui.c` layout-build pass using the `WIDGET_CELLS_*` constants pinned in T003 (Graph = `WIDGET_CELLS_GRAPH`, Fortune = `WIDGET_CELLS_FORTUNE = 2`, Activities = `WIDGET_CELLS_ACTIVITIES`, Repo Status = `WIDGET_CELLS_REPO_STATUS`).
- [~] T016 [US1] Build, lint, smoke-test on native (X11). Manually verify with a Redis stub that Row 1 / Rows 2-3 / footer composition matches FR-001 → FR-006. Explicitly confirm the status/gutter area renders identically to `main` (no font/size/color regression vs. sprint 005 baseline) — resolves analyze finding A2. [DEFERRED: build green, runtime smoke test pending operator verification on Pi.]

**Checkpoint**: US1 visually verifiable. US2/US3 may now populate the footer and graph areas.

---

## Phase 4: User Story 2 — Service Status Card goes red when a service stops reporting (Priority: P1)

**Goal**: End-to-end Service Status Card: registry parses Redis payloads, freshness/colour state machine drives the border, `kpidash-client service-status` publishes payloads, malformed payloads are ignored (FR-022a).

**Independent Test**: `kpidash-client service-status --name test --state ok --text "..."`; card appears green within 2 s; stop publishing; border transitions to red within 60–62 s; publish `down`; border stays gray. SC-006, SC-007, SC-008.

### Tests for User Story 2

- [X] T017 [P] [US2] In `tests/test_service_card.c` add: `service_parse_state` cases for each enum value + NULL + empty + `"bogus"` → UNKNOWN; `service_color()` truth-table per [data-model.md §2](data-model.md) for the cross-product of `last_valid_state × fresh ∈ {true,false}`; explicit DOWN-with-stale-ts → still GRAY; UNHEALTHY-just-aged-out → RED.
- [X] T018 [P] [US2] In `tests/test_service_card.c` add a JSON-parse case (using cJSON, mirroring `test_redis_json.c`) that exercises the future `redis_parse_service_payload(const char *json, service_entry_t *out)` over (a) a valid payload, (b) missing `state`, (c) unrecognized `state`, (d) missing `ts`, (e) missing `text`. Cases (b)–(e) MUST return a "do not update" signal so callers preserve `last_valid_state` / `last_payload_ts` (FR-022a).
- [X] T019 [P] [US2] In `tests/test_icon_registry.c` add: `icons_lookup(-1)` → NULL; `icons_lookup(99999)` → NULL; `icons_lookup(0)` → non-NULL UTF-8; `icons_codepoint()` mirrors. FR-023, FR-026.

### Implementation for User Story 2

- [X] T020 [P] [US2] In `src/icons.c` populate `ICON_REGISTRY[]` with an initial curated set (10–20 entries covering generic services: computer, server, database, queue, network, disk, lock, key, cloud, sync, etc.) using codepoints from the `0xF300-0xF381` range covered by the icon font (T010). Implement `icons_lookup()` as a linear scan returning a UTF-8 encoding of the codepoint (encode 0xF300–0xFFFF as 3-byte UTF-8). Implement `icons_codepoint()` similarly. Make T019 pass.
- [X] T021 [US2] In `src/redis.h` declare `int redis_parse_service_payload(const char *json, service_entry_t *out)` (0 = updated, -1 = ignore/malformed). In `src/redis.c` implement it: cJSON parse → require `ts` (number), `state` (string, parsed via `service_parse_state`, reject UNKNOWN), `text` (string); optional `host` and `icon` (integer); write into `*out` only if all required fields valid. Free the cJSON tree in all paths. Make T018 pass. FR-022, FR-022a.
- [X] T022 [US2] In `src/registry.{c,h}` add a service registry: fixed-size array (e.g. 32 entries) of `service_entry_t` with `service_registry_find_or_create(const char *name)`, `service_registry_apply_payload(service_entry_t *e, const service_entry_t *parsed)` (updates `last_valid_state`, `last_payload_ts`, `text`, `host`, `icon_index`; called only with parsed-OK data so the malformed-ignore rule is upheld by the caller path). Include a `service_registry_iter()` helper.
- [X] T023 [US2] In `src/redis.c`, add `void redis_poll_services(redisContext *c)` invoked from the existing 1 Hz poll path: `SCAN MATCH kpidash:services:*` (use the existing scan helper pattern), for each key `GET` the value, call `redis_parse_service_payload()`; on success, find-or-create the entry in the service registry and apply. On parse failure, log at debug level and continue (FR-022a). Per [contracts/redis-services.md](contracts/redis-services.md).
- [X] T024 [US2] Create `src/widgets/service_card.{c,h}`: `service_card_create(lv_obj_t *parent, service_entry_t *e)` builds the card (label + optional host + optional icon `lv_label` using `lv_font_icons_56` + status text + thick border) and stores LVGL handles on `e`; `service_card_update(service_entry_t *e, double now)` recomputes colour via `service_color()` and sets the border `lv_color_t` accordingly. Use a fixed thick-border style (final thickness during T040 mockup tuning).
- [X] T025 [US2] In `src/ui.c`, on each UI tick, iterate `service_registry_iter()`; for new entries create a card in the footer Service-Strip container (T013) using `service_card_create()`; for existing entries call `service_card_update(now)`. Cards lay out left-to-right with even spacing; assume the strip is wide enough (FR-017; per-card precise sizing finalized in T040 from the mockup).
- [X] T026 [P] [US2] In `clients/kpidash-client/kpidash_client/cli.py` add a `service-status` Click subcommand accepting `--name` (required), `--state` (choice: ok/unhealthy/maintenance/down, required), `--text` (required), `--host` (optional), `--icon` (optional int). Reuse the existing Redis-config loader. Build a JSON payload `{ ts: time.time(), state, text, host?, icon? }`, then `redis_client.set(f"kpidash:services:{name}", json.dumps(payload))`. FR-027, FR-028, FR-029.
- [X] T027 [P] [US2] In `clients/kpidash-client/tests/`, add `test_service_status.py` exercising the new subcommand via Click's `CliRunner`: valid invocation writes the expected key with a parseable JSON value containing the required fields; missing required flags fail with a non-zero exit code; choice validation rejects bogus states. Mock `redis.Redis` to capture the `set()` call.

**Checkpoint**: US2 end-to-end demonstrable via the quickstart §5 flow.

---

## Phase 5: User Story 3 — "NO NEW DATA" overlay on a stalled graph (Priority: P1)

**Goal**: Always-on graph; when a host's last sample is ≥ 30 s old, freeze the chart and overlay a centered "NO NEW DATA" label. Overlay clears when samples resume.

**Independent Test**: Start graph reporting from one host; observe live traces; stop the client; within 35 s the overlay appears (SC-003). Resume; overlay clears.

### Tests for User Story 3

- [X] T028 [P] [US3] In `tests/test_graph_router.c` add cases for the host-series helpers (T006): `graph_host_find_or_create` returns a stable pointer; `graph_host_touch` updates `last_sample_ts`; `graph_host_is_stale(now)` returns false at `now == last_sample_ts + 29.9` and true at `+30.0`; sentinel host `"(legacy)"` is accepted (edge case for unupgraded clients). Also add an explicit FR-015a no-eviction assertion: after `graph_host_find_or_create("x")` followed by `graph_host_is_stale` returning true for an arbitrarily long simulated wait, the series struct MUST still be reachable by a subsequent `graph_host_find_or_create("x")` returning the same pointer (no eviction code path exists). Resolves analyze finding A1.

### Implementation for User Story 3

- [X] T029 [US3] In `src/widgets/dev_graph.{c,h}`: convert each graph widget to a parent `lv_obj_t` container holding the existing `lv_chart` plus a hidden `lv_label` ("NO NEW DATA") centered via `LV_ALIGN_CENTER` using `lv_font_montserrat_bold_36` (or `_48`) with `LV_OBJ_FLAG_HIDDEN` set at creation. Per [research.md R2](research.md).
- [X] T030 [US3] In `src/widgets/dev_graph.c`, on each UI tick, evaluate `graph_host_is_stale(series, now)`; show or hide the overlay via `lv_obj_clear_flag` / `lv_obj_add_flag` with `LV_OBJ_FLAG_HIDDEN`; freeze chart updates while stale (the existing update path naturally pauses because no new sample arrives — confirm no clock-driven scrolling occurs). FR-014, FR-015.
- [X] T031 [US3] In `src/ui.c`, ensure there is **no** code path that creates or honours a `kpidash:cmd:graph` enable/disable command; remove any such control if present from sprint 003. Search-and-confirm via `grep -rni 'cmd:graph\|graph.*toggle' src/`. FR-010, SC-002.
- [~] T032 [US3] Build, deploy to rpi53 with the soak-binary swap process from sprint 005 quickstart. Verify with `journalctl` that graph updates continue and that stopping the local `kpidash-client` triggers the overlay within ~35 s.

**Checkpoint**: US3 verifiable on-target. US4 layers multi-host on top.

---

## Phase 6: User Story 4 — One Graph per reporting host (Priority: P2)

**Goal**: `host` field on each graph sample; one Graph widget per host; alphabetical placement; GPU usage + GPU VRAM traces thickened 2–3×.

**Independent Test**: Both `kdash` and `kai` reporting → two Graph widgets visible, `kai` first; only one reporting → one widget. GPU traces visibly thicker. SC-004, SC-005.

### Tests for User Story 4

- [X] T033 [P] [US4] In `tests/test_graph_router.c` add: a sequence of `redis_parse_dev_telemetry` (or equivalent) calls with `host="kdash"` and `host="kai"` route to two distinct series; samples missing `host` route to `"(legacy)"`; alphabetical ordering of two-host fixture matches `kai` before `kdash`. FR-011, FR-013.

### Implementation for User Story 4

- [X] T034 [US4] In `src/redis.c`'s existing `dev_telemetry` parser, read the new `host` field from the JSON payload; default to `"(legacy)"` when absent (edge case from spec). On each parsed sample, call `graph_host_find_or_create(host)` and `graph_host_touch(host, now)`, then apply the sample to that host's chart. FR-011, FR-012.
- [X] T035 [US4] In `src/ui.c`'s rows-2-3 build pass (T015), expand graph hosts into `widget_request_t` entries — one per discovered host series — sorted alphabetically by `host` **before** the stable `layout_pool_place()` sort so the alphabetical order is preserved among Graph entries. FR-013. [Per-host telemetry now lives on `graph_host_series_t.telemetry`; `redis.c` Step 8 writes each parsed sample into its host's slot; `src/ui.c` snapshots all hosts, sorts alphabetically, builds N WIDGET_GRAPH requests with `payload = host`, and creates/positions/updates a dev_graph widget per host via the series's `widget` slot. Pool overflow hides extra widgets. Legacy `g_dev_graph` single-host globals retired.]
- [X] T036 [US4] In `src/widgets/dev_graph.c`, thicken the GPU compute and GPU VRAM traces via the **triple-trace trick** (LVGL 9 has no per-series line-width API). For each of the two GPU series, add two flanking series (`+offset` / `-offset`) sharing the original colour, then plot all three with the same line width. Choose `offset` so the three traces visually merge into a band ~2.5× the single-trace width: for the primary-Y % series, `offset_pct = 0.4` (≈ 1 px at the current chart height of ~600 px); for the secondary-Y MB series, `offset_mb = vram_max / 240`. Document the chosen multiplier and the formula in a code comment. FR-016, SC-005.
- [X] T037 [US4] In `clients/kpidash-client/kpidash_client/`, add the local hostname (via `socket.gethostname()` or existing config field) to the dev-telemetry payload as `host`. Confirm legacy publishers (without the field) still render under `"(legacy)"`. Per [contracts/redis-dev-telemetry-host.md](contracts/redis-dev-telemetry-host.md).
- [~] T038 [US4] Deploy to rpi53. Manually trigger graph publishing from `kdash` and (after configuring per quickstart §6) `kai`. Confirm two graph widgets render in alphabetical order with the thickened GPU traces.

**Checkpoint**: US4 verifiable end-to-end.

---

## Phase 7: User Story 6 — Mockup target for footer-card iteration (Priority: P2)

**Goal**: `kpidash-mockup` native target builds and runs against X11, renders only the footer with stubbed Service Status Cards. Excluded from cross-compile and `TESTS_ONLY` builds.

**Independent Test**: `cmake --build build --target kpidash-mockup && ./build/kpidash-mockup` opens an X11 window with stubbed cards. `cmake --build build-pi` produces no mockup artefact. SC-010.

(US6 has no logic tests beyond build smoke; it is intentionally placed before US5 because Fortune polish is a lower-priority deliverable and the mockup is a developer-velocity tool that helps finalize FR-017 / FR-025 sizing.)

### Implementation for User Story 6

- [X] T039 [P] [US6] In root `CMakeLists.txt`, add `if(NOT CMAKE_CROSSCOMPILING AND NOT TESTS_ONLY) add_executable(kpidash-mockup src/mockup_main.c src/widgets/service_card.c src/icons.c fonts/lv_font_icons_56.c …) target_link_libraries(kpidash-mockup PRIVATE lvgl …) endif()`. Mirror the native-only link path used by the existing `kpidash` target for X11. FR-030, FR-031.
- [~] T040 [US6] **CUT** — superseded by live-iteration loop on rpi53 using seeded redis service-status entries (see `/memories/repo/kpidash-deploy.md`). The mockup binary's value (rapid card-dimension iteration) is delivered more cheaply by `cmake --build build-pi --target deploy` + `redis-cli SET kpidash:services:*` against the live dashboard. `src/mockup_main.c` remains a stdio stub for future use if needed.

**Checkpoint**: Footer card visuals locked in; US2 cards in the live dashboard inherit the tuned dimensions.

---

## Phase 8: User Story 5 — Fortune readable from across the room (Priority: P3)

**Goal**: Fortune renders as a 2×1 cell in rows 2-3 with the larger fixed font; the previous footer-Fortune is gone. (Core renderer landed in T014; this phase locks the font choice and adds the optional stretch.)

**Independent Test**: With Fortune in the requested-widgets list, observe a 2×1 Fortune in rows 2-3 with a noticeably larger font than the prior footer version. SC-009.

### Implementation for User Story 5

- [~] T041 [US5] Finalize the Fortune font selection from T014 (between Montserrat Bold 36 and 48) by visual inspection on rpi53. Confirm the chosen size in `src/widgets/fortune.c` matches the cell footprint and is legibly larger than the prior footer font. FR-008. [DEFERRED: current implementation uses bold_36; awaits operator visual sign-off on Pi.]
- [~] T042 [US5] **(Stretch — FR-009; may be deferred)** Implement simple dynamic Fortune sizing: estimate display width of the candidate fortune at the selected font; if the estimated rendered width would exceed the 2×1 cell, discard and fetch the next fortune (cap at e.g. 3 retries); otherwise, if the fortune is short, bump the font up one size class up to a max. Detection need not be perfect — occasional clipping is acceptable per the spec. Skip this task without blocking ship if time-pressed. [DEFERRED: stretch goal; LV_LABEL_LONG_WRAP at bold_36 handles most cases acceptably.]

**Checkpoint**: All P1/P2/P3 user stories complete.

---

## Phase 9: Polish & Cross-Cutting Concerns

**Purpose**: Documentation, validation, and ship gates.

- [X] T043 [P] In `docs/CLIENT-PROTOCOL.md`, add a new section under "Per-key contracts" for `kpidash:services:<name>` per [contracts/redis-services.md](contracts/redis-services.md), and extend the existing dev-telemetry section to document the new required `host` field per [contracts/redis-dev-telemetry-host.md](contracts/redis-dev-telemetry-host.md). (Flagged by the plan summary as scheduled during implementation.)
- [X] T044 [P] In `docs/ARCHITECTURE.md`, append short subsections for: the rows-2-3 priority pool placement engine; the Service Status Card lifecycle (parse → freshness → colour); the icon registry's append-only indexing rule. Cross-link to spec/plan.
- [~] T045 Final ship gate: clang-format dry-run on all touched C files; cppcheck on `src/`; native build + `ctest`; cross build via `build-pi`; deploy to rpi53; let it run ≥ 30 minutes confirming `memstat:` RSS trend remains flat (no regression vs sprint 005's 11.3 KB/h baseline); explicitly confirm the status/gutter area is unchanged from `main` (visual diff per FR-006). Record the result, plus the operator's subjective coherence sign-off for SC-011, in a brief `specs/006-layout-refresh-status-cards/findings.md`.
- [~] T046 Run the full [quickstart.md](quickstart.md) §1–§6 from a clean checkout; verify each numbered step matches observed behaviour; tick the requirements checklist at [checklists/requirements.md](checklists/requirements.md) where applicable.

---

## Dependencies & Execution Order

### Phase dependencies

- **Setup (Phase 1)**: no deps — start immediately.
- **Foundational (Phase 2)**: depends on Setup; **blocks all user stories**.
- **US1 (Phase 3)**: depends on T004–T006 (pool + host series structs); T011 [P] may proceed in parallel with T012.
- **US2 (Phase 4)**: depends on T007–T010 (service entry, icon font); independent of US1's UI placement (US2 writes into the footer container T013 creates).
- **US3 (Phase 5)**: depends on T006 (host series); independent of US1/US2.
- **US4 (Phase 6)**: depends on US3 (Phase 5 — extends the same `dev_graph` widget) and on T015 (graph widget registration in the pool).
- **US6 (Phase 7)**: depends on T009/T010 (icon registry + font) and on the existence of `service_card.c` (T024).
- **US5 (Phase 8)**: depends on T014; T042 is explicit stretch.
- **Polish (Phase 9)**: depends on all desired user stories.

### User story story-level dependencies

- US1 must complete T012/T013 before US2's T025 and US3/US4's graph placement land in the live layout.
- US3 must complete before US4 (US4 builds on the per-host series model).
- US6 may begin in parallel with US2 once T009/T010/T024 land.

### Parallel opportunities

- **Within Setup**: T002 ∥ T003.
- **Within Foundational**: T007 ∥ T009 ∥ T010 once T004 lands. T005/T006/T008 are sequential within their own files.
- **Tests-vs-impl per story**: every `tests/test_*` task marked [P] may proceed in parallel with the implementation tasks in its phase, fulfilling the TDD Red-Green requirement (write the test → confirm it fails → land the impl → confirm it passes).
- **Cross-story**: once Foundational completes, US1 ∥ US2 ∥ US3 ∥ US6 may be worked in parallel by separate workers; US4 waits for US3; US5 waits for T014.
- **Within Polish**: T043 ∥ T044.

### Within each user story

- Tests (the [P]-marked test tasks) MUST be written and FAIL before the corresponding implementation tasks land (per the constitution's TDD principle).
- Headers / struct declarations land before the implementation TUs that depend on them.
- Layout and registry plumbing land before widget creation.
- Widget creation lands before UI integration.

---

## Suggested MVP scope

**MVP = US1 + US2 + US3** (all P1). This delivers the visible reshape (rows 1/2-3/footer composition), end-to-end Service Status Cards including the green→red transition, and the always-on graph with NO-NEW-DATA overlay. US4 (multi-host graph) and US6 (mockup) and US5 (Fortune polish) can ship in the same sprint but are independently droppable if time-pressed.

## Parallel Example: User Story 2 fan-out

```bash
# After Phase 2 completes, three workers can pick up US2 in parallel:
#   worker A: T017 (state tests) → T018 (parser tests)  → T021 (parser impl)
#   worker B: T019 (icon tests)  → T020 (icon registry impl)
#   worker C: T026 (client subcommand) → T027 (client tests)
# Then a single integrator merges: T022 (registry struct) → T023 (poll) → T024 (widget) → T025 (UI wire-up).
```

---

## Format validation

All 46 tasks above conform to: `- [ ] T0XX [P?] [USx?] description with file path`. Setup, Foundational, and Polish tasks intentionally carry no `[USx]` label. Every implementation task names the file(s) it touches.
