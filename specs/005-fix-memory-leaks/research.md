# Phase 0 Research: Memory Leak Investigation

**Feature**: 005-fix-memory-leaks  
**Date**: 2026-05-16

This document resolves all "NEEDS CLARIFICATION" items from the Technical Context before design begins. Each item lists the decision, the rationale, and alternatives considered.

---

## R1 — How to sample the LVGL heap from inside kpidash

- **Decision**: Enable `LV_USE_MEM_MONITOR` in `lv_conf.h` and call `lv_mem_monitor(&mon)` from a periodic timer. Sample fields: `total_size`, `free_size`, `used_pct` (computed), `frag_pct`, `max_used`, `free_biggest_size`.
- **Rationale**: First-party LVGL API. Zero new dependencies. Already supported by the project's pinned LVGL 9.2.2. Fragmentation and `max_used` are exactly the signals needed to distinguish "churn within a steady heap" from "monotonic growth".
- **Alternatives considered**:
  - Replacing LVGL's allocator with a custom one that records every alloc — far too invasive for the value, and would change behavior under test vs. production.
  - External Valgrind/massif runs — useful but cannot be left running on the production Pi and were the source of the original incident (installing tooling restarted the service).

## R2 — How to sample process RSS without installing host tooling

- **Decision**: Read `/proc/self/statm`, take the second field (resident pages), multiply by `sysconf(_SC_PAGESIZE)`. Optionally also read field 1 (size) for VmSize and field 6 (data) for data-segment size.
- **Rationale**: Always present on Linux. No external command spawn. No additional package install. Reading `/proc/self/statm` is a single small read per minute — negligible overhead.
- **Alternatives considered**:
  - `getrusage(RUSAGE_SELF)` — reports `ru_maxrss` (peak), not current; useful as a secondary signal but cannot replace current RSS.
  - Parsing `/proc/self/status` — provides VmRSS, VmHWM, VmPeak, VmSwap as labeled fields; richer but requires text parsing. Acceptable fallback if the simple `statm` form proves insufficient during implementation; the design allows swapping the source without changing the public sample shape.
  - Spawning `ps` / `smem` — explicitly rejected (caused the original incident).

## R3 — Telemetry sampling cadence

- **Decision**: Sample every 60 s, emit one log line and update the Redis `current` key on every sample; LPUSH/LTRIM the `ring` key to keep the last 1500 samples (~25 h).
- **Rationale**: 60 s gives ≥ 1440 points across a 24 h soak — plenty for trend analysis. One log line per minute does not dominate the log. 1500 entries × ~120 B JSON ≈ 180 KB in Redis — trivial.
- **Alternatives considered**:
  - 1 s (same cadence as the existing poll) — wastes log space and Redis storage with no diagnostic value; LVGL heap moves slowly enough that minute-resolution is sufficient.
  - 5 min — too coarse to localize a leak that ramps over an hour or two.

## R4 — How to drive widget update functions headlessly for the leak regression test

- **Decision**: Initialize LVGL in the test (`lv_init()`) without registering any display driver, manually drive `lv_timer_handler()` only enough to flush deferred work, then call each widget's `*_update` entry point N≥1000 times with representative payloads. Bracket the loop with `lv_mem_monitor()` calls and assert that `max_used_after − max_used_warm` is below a tight cap.
- **Rationale**: All target widget code paths are `*_update(lv_obj_t *, ...)` and operate on objects parented under a screen — they do not require a real display backend. The existing `tests/test_layout.c` already creates LVGL objects in a unit test, confirming the pattern works.
- **Alternatives considered**:
  - Running tests against a real DRM display — not feasible in CI and not necessary to validate object lifecycle.
  - Using a custom LVGL stub allocator that tracks every allocation — duplicates what `lv_mem_monitor` already provides.

## R5 — Soak post-processing and pass/fail verdict

- **Decision**: The kpidash log lines for memory samples follow a fixed prefix (e.g. `memstat: t=<unix> rss=<bytes> lvgl_used=<bytes> lvgl_max=<bytes> lvgl_frag=<pct>`). The quickstart documents two equivalent one-liners — `awk` and Python — that:
  1. Filter `memstat:` lines.
  2. Find the sample whose `t` is closest to (start + 3600 s) → baseline.
  3. Find the last sample → final.
  4. Compute `rss_ratio = final.rss / baseline.rss` and `lvgl_ratio = final.lvgl_max / baseline.lvgl_max`.
  5. Pass iff `rss_ratio ≤ 1.5` (SC-001) and `lvgl_ratio ≤ 1.2` (SC-002).
- **Rationale**: Plain text + standard tools = reproducible verdict on any machine, no extra dependencies, no risk of post-processing being the source of the next incident.
- **Alternatives considered**:
  - Emitting JSON Lines — slightly easier to parse but verbose in the log and harder to read at the terminal during live debugging. The fixed key=value form is greppable, jq-able after a trivial conversion, and human-readable.
  - Storing samples only in Redis — coupling the verdict to Redis state means a Redis restart loses the soak history. Log is the source of truth; Redis is a convenience.

## R6 — Where to suspect leaks first (prioritized audit list)

Not a research item per se, but recorded here so Phase 2 work is targeted. Ordered by suspicion based on the `.scratch/mem-leak.md` discussion and a scan of `src/widgets/`:

1. **`src/widgets/repo_status.c::repo_status_widget_update`** — deletes and rebuilds all grid children every time the repo set changes; high churn under registry/repo activity.
2. **`src/widgets/activities.c::activities_widget_update`** — same delete-and-rebuild pattern on every change.
3. **`src/ui.c` add/remove card path** — when registry churn causes `add_card` / `remove_absent_cards` to run, verify the compacted arrays do not leak label/style references.
4. **`src/widgets/dev_graph.c::dev_graph_destroy` + recreate on `g_graph_client` change** — `lv_chart_set_point_count` allocates per-series arrays; ensure `dev_graph_destroy` is always called (and only once) before recreation.
5. **Redis error banner in `src/ui.c` (`g_redis_err`) and `status_bar`** — verify reuse instead of recreate on each connect/disconnect cycle.

These are starting suspects; the telemetry from R1+R2 plus the regression test from R4 will confirm or refute each.

---

All Technical Context items resolved. Proceeding to Phase 1.
