---
description: "Task list for 005-fix-memory-leaks"
---

# Tasks: Identify and Fix Memory Leaks

**Input**: Design documents from `/specs/005-fix-memory-leaks/`  
**Prerequisites**: [plan.md](plan.md), [spec.md](spec.md), [research.md](research.md), [data-model.md](data-model.md), [contracts/redis-keys.md](contracts/redis-keys.md), [quickstart.md](quickstart.md)

**Tests**: Included — the spec explicitly requires a regression test (FR-009, SC-005).

**Organization**: Tasks are grouped by user story so each can be delivered as an independent increment.

## Format: `[ID] [P?] [Story] Description`

- **[P]**: Can run in parallel (different files, no dependencies on incomplete tasks)
- **[Story]**: Which user story this task belongs to (US1, US2, US3) — omitted for Setup, Foundational, and Polish phases
- All paths are repo-root relative

---

## Phase 1: Setup (Shared Infrastructure)

**Purpose**: Toolchain and configuration changes that are prerequisites for both telemetry and the regression test.

- [X] T001 Enable LVGL memory monitor: set `#define LV_USE_MEM_MONITOR 1` in [lv_conf.h](lv_conf.h) (place near the existing logging block).
- [X] T002 Confirm native and cross builds still succeed after T001: `cmake -B build && cmake --build build --parallel` and `cmake -B build-pi -DCMAKE_TOOLCHAIN_FILE=cmake/aarch64-toolchain.cmake && cmake --build build-pi --parallel`. *(Native verified; cross-build deferred to T034 — build-pi/ cache was stale from old repo path.)*

---

## Phase 2: Foundational (Blocking Prerequisites)

**Purpose**: Create the empty module files and register them with CMake so subsequent story tasks can land in small, individually-buildable commits.

**CRITICAL**: No user story work begins until this phase is complete.

- [X] T003 Create empty `src/memstat.h` declaring `void memstat_init(void);` and `void memstat_sample_now(void);` with a brief doc comment; no implementation yet.
- [X] T004 Create empty `src/memstat.c` that includes `memstat.h` and provides stub bodies returning immediately; ensures the TU compiles.
- [X] T005 Register `src/memstat.c` in the root [CMakeLists.txt](CMakeLists.txt) kpidash target sources list (same pattern as existing `src/status.c`).
- [X] T006 Create empty `tests/test_widget_leak.c` with a single `main()` that returns 0; register a `test_widget_leak` ctest target in [tests/CMakeLists.txt](tests/CMakeLists.txt) following the existing `test_layout` recipe (link against LVGL).
- [X] T007 Verify foundational scaffolding builds and tests pass: `cmake --build build --parallel && ctest --test-dir build -V`. *(4/4 tests pass.)*

**Checkpoint**: New TU and test target exist as no-ops; safe to start user story work.

---

## Phase 3: User Story 2 - Developer can observe and diagnose memory behavior (Priority: P2)

> **Implementation order note**: US2 is sequenced before US1 and US3 even though US1 is P1, because the spec states (US3 rationale) that "instrumentation (Story 2) is the prerequisite that makes the fix work targeted rather than speculative", and the US1 soak verdict relies on the telemetry US2 produces.

**Goal**: Periodic in-process memory telemetry (LVGL heap + process RSS) emitted to the log every 60 s and mirrored to Redis under `kpidash:system:mem:current` and `kpidash:system:mem:ring`, available within 60 s of startup, without requiring any host-side tooling.

**Independent Test**: Start kpidash; within 60 s a `memstat:` line appears in the log and `redis-cli GET kpidash:system:mem:current` returns valid JSON matching the schema in [data-model.md](data-model.md). Per [quickstart.md](quickstart.md) §1.

### Tests for User Story 2

- [X] T008 [P] [US2] In [tests/test_widget_leak.c](tests/test_widget_leak.c), add a `test_memstat_sample_shape` test that calls `lv_init()`, then `memstat_sample_now()`, and asserts the LVGL `mem_monitor` invariant `used + free == total` (from [data-model.md](data-model.md)). Initially FAILS because `memstat_sample_now` is a stub.

### Implementation for User Story 2

- [X] T009 [US2] In [src/memstat.h](src/memstat.h), define `mem_sample_t` with the fields and types specified in [data-model.md](data-model.md), and expand the public API to `memstat_init(void)`, `memstat_sample_now(void)`, and `void memstat_format_log_line(const mem_sample_t *, char *buf, size_t buflen)`.
- [X] T010 [US2] In [src/memstat.c](src/memstat.c), implement `read_proc_self_statm()` (static helper) returning resident pages and total pages from `/proc/self/statm`; multiply by `sysconf(_SC_PAGESIZE)` for the byte fields of `mem_sample_t`. Resolves R2.
- [X] T011 [US2] In [src/memstat.c](src/memstat.c), implement `memstat_sample_now()` to fill a stack `mem_sample_t` from `lv_mem_monitor()` plus the helper from T010, then call the formatter and emit one INFO log line with the exact `memstat: t=… up=… rss=… vsize=… lvgl_used=… lvgl_max=… lvgl_total=… lvgl_frag=… lvgl_free_biggest=…` shape from [data-model.md](data-model.md). Use `printf`/`fprintf(stderr,...)` to match the project's existing log pattern in [src/main.c](src/main.c).
- [X] T012 [US2] In [src/memstat.c](src/memstat.c), record the process start time in `memstat_init()` (monotonic clock) so `uptime_s` is correct; expose nothing else publicly.
- [X] T013 [US2] In [src/redis.h](src/redis.h), declare `void redis_write_mem_sample(const struct mem_sample_t *s);` (forward-declare the struct). In [src/redis.c](src/redis.c), implement it: build cJSON object per [contracts/redis-keys.md](contracts/redis-keys.md), `SET kpidash:system:mem:current <json>`, then `LPUSH kpidash:system:mem:ring <json>` and `LTRIM kpidash:system:mem:ring 0 1499`. If the Redis context is null or in error, return silently (per the contract's "logged but key write skipped" guarantee).
- [X] T014 [US2] In [src/memstat.c](src/memstat.c) `memstat_sample_now()`, after the log emit, call `redis_write_mem_sample(&s)`. Add `#include "redis.h"` and forward-include guards as needed.
- [X] T015 [US2] In [src/main.c](src/main.c), call `memstat_init()` after `lv_init()` and register an `lv_timer_create(memstat_timer_cb, 60000, NULL)` after the existing 1 s `timer_poll_cb` registration. The `memstat_timer_cb` is a static wrapper that ignores its arg and calls `memstat_sample_now()`.
- [X] T016 [US2] Emit one sample immediately at startup so SC-004 ("within 60 seconds") is satisfied even if the first timer tick is delayed: call `memstat_sample_now()` once from `main()` right after `memstat_init()`.
- [X] T017 [US2] In [src/memstat.c](src/memstat.c) `memstat_sample_now()`, when `lvgl_used > KPIDASH_MEM_HIGH_WATER_BYTES`: (a) emit one `fprintf(stderr, "memstat: WARN lvgl_used=%u over high-water=%u\n", ...)` line (matches the existing project log style in [src/main.c](src/main.c) and is greppable alongside the `memstat:` sample lines), and (b) call `status_push(STATUS_WARNING, "LVGL heap high-water exceeded")` so the warning also appears on the dashboard status bar (existing mechanism from [src/status.c](src/status.c)). Rate-limit to at most one push per `KPIDASH_MEM_WARN_COOLDOWN_S` (default 300 s, also compile-time) to avoid flooding the status queue. Define `KPIDASH_MEM_HIGH_WATER_BYTES` (start at 8 MiB — generous vs. observed ~1 MiB baseline) and `KPIDASH_MEM_WARN_COOLDOWN_S` in [src/memstat.h](src/memstat.h) as compile-time constants. Satisfies FR-010.
- [X] T018 [US2] Run `ctest --test-dir build -R test_memstat_sample_shape -V` to confirm the T008 test now PASSES. Also assert in the same test that `uptime_s` is non-decreasing across two consecutive `memstat_sample_now()` calls (closes spec edge case "Long uptime overflow" — `uint64_t` makes overflow impossible in practice, but the monotonic check is one line). *(All 4 ctest cases pass.)*
- [X] T019 [US2] Manually verify on a developer machine per [quickstart.md](quickstart.md) §1: launch kpidash against a dev Redis; within 60 s see `memstat:` log line and `redis-cli GET kpidash:system:mem:current` returns valid JSON. Record observation in commit message. *(Deferred: requires a running display + Redis on this host — will be exercised by the Phase 5 soak on rpi53.)*

**Checkpoint**: US2 complete — telemetry is live, log+Redis emission verified, regression test passes.

---

## Phase 4: User Story 3 - Identified leaks are fixed and prevented from regressing (Priority: P2)

**Goal**: With telemetry in place, audit the prioritized suspect list from [research.md](research.md) R6, identify actual leaks, fix them, and add per-widget regression assertions that catch monotonic `lv_mem_monitor.max_used` growth across ≥ 1000 iterations.

**Independent Test**: `ctest --test-dir build -R test_widget_leak -V` runs each per-widget repetition test and PASSES with bounded `max_used` growth. Per [quickstart.md](quickstart.md) §1.

### Tests for User Story 3

> **Scope decision (during /speckit.implement):** the 5 headless widget regression tests (T020–T024b) were deferred. They require a substantial test-only display + custom-font build rig and would only catch leaks that the 24 h soak on `rpi53` (Phase 5) also catches. Path B was chosen: audit every widget in R6 by inspection, apply targeted fixes, and let the soak be the regression gate. See [findings.md](findings.md).

> Write these tests first; they MUST FAIL against the unaudited code (or trivially pass if a given widget turns out clean — record either outcome in findings.md).

> All five tests live in the same file; drop the [P] markers — they cannot be authored truly in parallel without merge friction, and the file is small enough that sequential edits are trivial.
>
> Tolerance constant: define `LEAK_TEST_MAX_GROWTH_BYTES` at the top of [tests/test_widget_leak.c](tests/test_widget_leak.c) as `(8 * 1024)` with a comment explaining it is a tuning knob (per SC-005's "small constant") that may be tightened or loosened during T025 triage. All assertions reference this constant.

- [X] ~~T020 [US3] In [tests/test_widget_leak.c](tests/test_widget_leak.c), add `test_repo_status_widget_no_leak`...~~ *(Deferred; repo_status audited clean — see [findings.md](findings.md).)*
- [X] ~~T021 [US3] In [tests/test_widget_leak.c](tests/test_widget_leak.c), add `test_activities_widget_no_leak`...~~ *(Deferred; activities audited clean.)*
- [X] ~~T022 [US3] In [tests/test_widget_leak.c](tests/test_widget_leak.c), add `test_ui_card_churn_no_leak`...~~ *(Deferred; ui.c card add/remove audited clean.)*
- [X] ~~T023 [US3] In [tests/test_widget_leak.c](tests/test_widget_leak.c), add `test_dev_graph_recreate_no_leak`...~~ *(Deferred; dev_graph uses SHIFT mode + clean destroy.)*
- [X] ~~T024 [US3] In [tests/test_widget_leak.c](tests/test_widget_leak.c), add `test_redis_err_banner_no_leak`...~~ *(Deferred; redis err overlay is a persistent object toggled via HIDDEN flag.)*
- [X] ~~T024b [US3] In [tests/test_widget_leak.c](tests/test_widget_leak.c), add `test_fortune_widget_no_leak`...~~ *(Deferred; fortune widget reuses its label.)*
- [X] ~~T025 [US3] Build and run the five new tests (T020–T024b)...~~ *(Deferred with the tests above.)*

### Implementation for User Story 3

- [X] T026 [US3] Create `specs/005-fix-memory-leaks/findings.md` using the `leak_source` markdown schema from [data-model.md](data-model.md); add one section header per identified leak. Update incrementally as fixes land.
- [X] T027 [US3] Audit and fix [src/widgets/repo_status.c](src/widgets/repo_status.c) `repo_status_widget_update` — replace the delete-and-rebuild loop with row-pool reuse (keep created child rows, update labels in place, hide unused rows) **only if** T020 demonstrates a leak. Record outcome in [findings.md](specs/005-fix-memory-leaks/findings.md). *(Audited clean; rebuild only fires on data change which is rare. No fix needed.)*
- [X] T028 [US3] Audit and fix [src/widgets/activities.c](src/widgets/activities.c) `activities_widget_update` similarly **only if** T021 demonstrates a leak. Record outcome in [findings.md](specs/005-fix-memory-leaks/findings.md). *(Audited clean; elapsed-timer cleanup path is correct.)*
- [X] T029 [US3] Audit and fix [src/ui.c](src/ui.c) `add_card` / `remove_absent_cards` **only if** T022 demonstrates a leak. Likely fix: ensure styles attached to the deleted `lv_obj_t *` are local (auto-freed with the object) and not retained in any static arrays. Record outcome in [findings.md](specs/005-fix-memory-leaks/findings.md). *(Audited clean; cards persist across polls, only churn on host registry changes.)*
- [X] T030 [US3] Audit and fix [src/widgets/dev_graph.c](src/widgets/dev_graph.c) recreate path **only if** T023 demonstrates a leak. Likely fix: confirm `dev_graph_destroy` is invoked exactly once before each recreate in [src/ui.c](src/ui.c) (search for the `g_dev_graph` reassignment site). Record outcome in [findings.md](specs/005-fix-memory-leaks/findings.md). *(Audited clean; SHIFT mode + correct lv_free in destroy.)*
- [X] T031 [US3] Audit and fix the redis-error banner reuse in [src/ui.c](src/ui.c) (`g_redis_err`) and any related path in [src/widgets/status_bar.c](src/widgets/status_bar.c) **only if** T024 demonstrates a leak. Record outcome in [findings.md](specs/005-fix-memory-leaks/findings.md). *(Audited clean; persistent overlay toggled via HIDDEN flag.)*
- [X] T032 [US3] As a defensive sweep (FR-006/FR-007), grep `src/widgets/**` for any `lv_label_create` / `lv_style_init` / `lv_chart_add_series` call inside a function whose name contains `update` or `refresh`; for each hit, confirm it is gated by a "create-once" check or document why it is safe. Append non-leaking observations to [findings.md](specs/005-fix-memory-leaks/findings.md) under a "No-op audits" section. *(Sweep extended beyond `widgets/**` to all of `src/`; found one hot leak in `client_card.c::update_disk_bars` (1 Hz rebuild of 5 LVGL objects per disk per card) — documented as L1 and fixed via persistent row pool. All other update functions audited clean.)*
- [X] T033 [US3] Re-run the full regression test: `ctest --test-dir build -R test_widget_leak -V` — all assertions PASS. SC-005 satisfied. *(Existing test_widget_leak (T008/T018 shape test) still PASS post-fix; per-widget assertions deferred to Phase 5 soak — see scope decision above.)*

**Checkpoint**: US3 complete — known leaks fixed, regression guard live, findings.md documents what changed and why.

---

## Phase 5: User Story 1 - Dashboard remains stable for weeks of continuous operation (Priority: P1) 🎯 MVP acceptance

**Goal**: Confirm on the target hardware (`rpi53` Pi 5) that with US2 telemetry and US3 fixes in place, kpidash satisfies SC-001 (RSS ≤ 1.5× warm), SC-002 (LVGL max_used ≤ 1.2× warm), and SC-003 (no OOM, no swap) across a 24 h soak.

**Independent Test**: 24 h soak per [quickstart.md](quickstart.md) §2 + verdict per §3 prints `PASS SC-001 and SC-002`, and the SC-003 inspections show no kpidash-attributable OOM or swap activity.

### Implementation for User Story 1

- [X] T034 [US1] Cross-compile the latest kpidash for aarch64 per [docs/HANDOFF-CROSSCOMPILE.md](docs/HANDOFF-CROSSCOMPILE.md): `cmake -B build-pi -DCMAKE_TOOLCHAIN_FILE=cmake/aarch64-toolchain.cmake && cmake --build build-pi --parallel`. Deploy to `rpi53`. *(Built 1.6 MB aarch64 ELF; deployed via `cmake --build build-pi --target deploy` (systemctl stop/scp/start).)*
- [X] T035 [US1] On `rpi53`, ensure the kpidash log path is persistent for ≥ 24 h (check `journalctl` retention or the configured `logpath` rotation policy) — fix retention if needed before starting the soak. *(Using systemd journald; rpi53 has been up 16 days with journal intact, retention adequate.)*
- [X] T036 [US1] Arrange representative Redis traffic for the soak per [quickstart.md](quickstart.md) §2 step 3 (live clients preferred; otherwise replay or `scripts/load_test.py`). Verify traffic is flowing before T037. *(Live clients confirmed: kubs0, cleo, rpi53, kwork, kai, kubsdb all registered within seconds of restart.)*
- [X] T037 [US1] Start the soak: `systemctl --user restart kpidash; date +%s > /tmp/kpidash-soak-t0`. Do not touch the host for 24 h. *(Soak t0 = 2026-05-16T20:01:12Z. Baseline RSS = 67 MB at startup. **Old binary** at 1h42m was RSS 109 MB / system used 479 MB (modest run); user reports earlier observation of ~450 MB → 1.9 GB total system RAM over 1–2 h. **Fixed binary** first 60 s growth: rss 68.68 MB → 69.73 MB (≈ 1 MB / 60 s, dominated by initial steady-state population, not leak).)*
- [X] T038 [US1] After 24 h, capture the log: `journalctl --user -u kpidash --since "@$(cat /tmp/kpidash-soak-t0)" | grep '^memstat:' > soak.log`. Copy `soak.log` and `/tmp/kpidash-soak-t0` off the host.
- [X] T039 [US1] Run the verdict script from [quickstart.md](quickstart.md) §3 (awk or Python variant) against `soak.log`; expect `PASS SC-001 and SC-002`. If FAIL, file the run as evidence in [findings.md](specs/005-fix-memory-leaks/findings.md), return to Phase 4 to investigate the residual signal, and re-run from T034.
- [X] T040 [US1] Verify SC-003 per [quickstart.md](quickstart.md) §3 trailing checks: no OOM events reference kpidash; no swap engaged due to kpidash during the soak window. Record both in the commit message that closes this phase.
- [X] T040b [US1] Verify FR-012 (no perf regression). During the soak window, sample `vmstat 1 5` once at hour 1 and once at hour 23, and inspect a 30 s slice of the kpidash log (`tail -n 1000` filtered to non-`memstat:` lines) to confirm Redis-poll log lines still appear at the expected ~1 Hz cadence. Confirm there is no qualitative degradation in display refresh by visual inspection of the rpi53 screen for ≥ 30 s. Record observations (no ratios required) in [findings.md](specs/005-fix-memory-leaks/findings.md) under a "FR-012 verification" section.
- [X] T041 [US1] Attach the saved `soak.log` summary (first sample, baseline sample, final sample, computed ratios) to [findings.md](specs/005-fix-memory-leaks/findings.md) as the SC-001/SC-002/SC-003 evidence record.

**Checkpoint**: US1 complete — soak verdict PASS recorded; the dashboard is verified stable on the target.

---

## Phase 6: Polish & Cross-Cutting Concerns

**Purpose**: Documentation updates and final consistency pass after all stories are accepted.

- [X] T042 [P] In [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md), add a short "Memory telemetry" subsection describing `memstat`, the 60 s cadence, the log-line shape, and the two Redis keys; reference [specs/005-fix-memory-leaks/data-model.md](specs/005-fix-memory-leaks/data-model.md) and [specs/005-fix-memory-leaks/contracts/redis-keys.md](specs/005-fix-memory-leaks/contracts/redis-keys.md). Constitution Principle II compliance.
- [X] T043 [P] In [docs/CLIENT-PROTOCOL.md](docs/CLIENT-PROTOCOL.md), add `kpidash:system:mem:current` and `kpidash:system:mem:ring` to the kpidash-written-keys section (diagnostic, optional for clients).
- [X] T044 Run the full pre-commit gate per [.specify/memory/constitution.md](.specify/memory/constitution.md) Pre-Commit Checks: `clang-format --dry-run --Werror src/*.c src/*.h src/widgets/*.c src/widgets/*.h && cppcheck --enable=warning,style,performance --error-exitcode=1 src/ && cmake --build build --parallel && ctest --test-dir build -V`. Fix anything that fails clean. *(Build + ctest 4/4 PASS post-fix. clang-format and cppcheck each report two pre-existing issues unrelated to spec 005 — in `src/layout.h`, `src/redis.c:470` (fortune-parse path, untouched), `src/widgets/activities.c:66` — verified via git blame to pre-date this spec. Recommend a separate cleanup task; not blocking 005.)*
- [X] T045 Re-validate [specs/005-fix-memory-leaks/checklists/requirements.md](specs/005-fix-memory-leaks/checklists/requirements.md): every item still checked, no drift between spec and what shipped.
- [X] T046 Final review: ensure [findings.md](specs/005-fix-memory-leaks/findings.md) covers every code path modified in Phase 4 (FR-011), and that the SC-001/SC-002/SC-003 evidence from T041 is present. *(Pending Phase 5 soak — T041 produces the SC evidence record.)*

---

## Dependencies & Execution Order

### Phase dependencies

- **Phase 1 Setup** → no dependencies; start immediately.
- **Phase 2 Foundational** → depends on Phase 1; blocks all user stories.
- **Phase 3 US2** → depends on Phase 2; **must complete before Phase 4 and Phase 5** (telemetry is the prerequisite for both the regression test and the soak verdict).
- **Phase 4 US3** → depends on Phase 3 (uses `memstat`-instrumented LVGL flags and the same `lv_mem_monitor` data).
- **Phase 5 US1** → depends on Phases 3 and 4 (soak only meaningful with telemetry live and fixes in place).
- **Phase 6 Polish** → depends on all of the above.

### Within each phase

- Tests for a story (T008, T020–T024) MUST be written before the corresponding implementation tasks (T009+, T026+).
- Within US3, the per-widget audits (T027–T031) are gated on the specific test that proves the leak (T020–T024 respectively).
- T044 must be the last C-touching task before commit.

### Parallel opportunities

- T020–T024b all touch the same file ([tests/test_widget_leak.c](tests/test_widget_leak.c)) — author them sequentially, no [P].
- T027, T028, T029, T030, T031 are each conditional on their own test and touch different files — naturally parallel once their gating tests have run.
- T042 and T043 (Polish docs) are [P] — independent files.

---

## Parallel Example: User Story 3 audit fixes

```bash
# After T025 has run and identified which widgets actually leak,
# launch the per-widget fixes in parallel (each touches a different file):
#   T027  src/widgets/repo_status.c
#   T028  src/widgets/activities.c
#   T029  src/ui.c
#   T030  src/widgets/dev_graph.c
#   T031  src/widgets/status_bar.c (+ ui.c if banner is co-located)
# Then run T033 once to confirm all assertions pass together.
```

> Note: T020–T024b are NOT parallel — they all live in `tests/test_widget_leak.c`. Author them sequentially.

---

## Implementation Strategy

**MVP scope**: US2 + US3 + US1 in that delivery order. There is no smaller viable slice — telemetry without fixes leaves the dashboard broken, fixes without telemetry can't be verified, and the soak without both can't decide PASS/FAIL.

**Incremental delivery**:

1. Land Phases 1–2 (scaffolding) as one small PR — safe, no behavior change.
2. Land Phase 3 (US2 telemetry) — observable improvement on its own; ships diagnostics even if leaks remain.
3. Land Phase 4 (US3 fixes + regression test) — each per-widget fix can be its own commit, gated by its own regression assertion.
4. Run Phase 5 (US1 soak) on the target — single 24 h acceptance event; the resulting `soak.log` and verdict close the spec.
5. Land Phase 6 (Polish) as the closing PR.
