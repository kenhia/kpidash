# Implementation Plan: Identify and Fix Memory Leaks

**Branch**: `005-fix-memory-leaks` | **Date**: 2026-05-16 | **Spec**: [spec.md](spec.md)  
**Input**: Feature specification from `/specs/005-fix-memory-leaks/spec.md`

## Summary

Investigate and resolve unbounded memory growth in the `kpidash` C daemon (observed climbing to ~7.7 GB RSS on the Pi 5 host `rpi53` from a ~67 MB baseline). Approach: (1) add in-process memory telemetry (LVGL heap monitor + procfs RSS) emitted to both the log and Redis so future incidents are observable without disturbing the running service; (2) write a documented soak procedure and a focused regression harness that asserts bounded LVGL `max_used` over many iterations of the suspect code paths; (3) audit and fix the widget update paths (`repo_status`, `activities`, registry-driven card add/remove, redis-error banner) for object/style/series/buffer reuse, then re-run the soak on the target hardware to confirm the SC-001/SC-002 envelopes hold.

## Technical Context

**Language/Version**: C11 (matches existing `src/`)  
**Primary Dependencies**: LVGL 9.2.2 (vendored under `lib/lvgl/`, configured by root `lv_conf.h`), hiredis, cJSON, Linux DRM/KMS  
**Storage**: Redis (existing keys under `kpidash:*`); telemetry adds new keys under `kpidash:system:mem:*`  
**Testing**: CTest under `tests/` (existing pattern: `test_config`, `test_layout`, `test_redis_json`); new test `test_widget_leak` exercises widget update functions and asserts bounded `lv_mem_monitor` growth  
**Target Platform**: Linux on Raspberry Pi 5 (aarch64, DRM/KMS) for the acceptance soak; native x86_64 Linux for unit tests and the smoke variant  
**Project Type**: Single embedded/desktop C application (no new project added)  
**Performance Goals**: Preserve existing display refresh (~30 fps, `LV_DEF_REFR_PERIOD 33`) and the 1 s Redis poll cadence; telemetry sampling at 60 s adds no measurable overhead  
**Constraints**: Telemetry MUST NOT require installing host-side tooling (the original incident was destroyed by `apt install smem`); telemetry path MUST work when Redis is disconnected (log only) and resume when reconnected  
**Scale/Scope**: Single kpidash process per host; ≤ `MAX_CLIENTS` cards in the registry; soak runs 24 h on the target Pi 5; regression harness exercises ≥ 1000 update iterations per widget

## Constitution Check

Evaluated against `.specify/memory/constitution.md` v1.2.0. All gates PASS.

| Principle | Status | Notes |
|---|---|---|
| I. Spec-Driven Development | PASS | Scoped to `specs/005-fix-memory-leaks/`; no out-of-spec changes. |
| II. Documentation in `docs/` | PASS | A short "Memory telemetry" subsection will be added to `docs/ARCHITECTURE.md` during polish (per the "kept reasonably current" clause); no new architecture doc required. |
| III. Test-Driven Development | PASS | New CTest target `test_widget_leak` is written before the widget audit/fix work. GUI code on real hardware is exempt and validated by the soak. |
| IV. Code Standards Gate | PASS | All new and modified C lives under `src/` and `src/widgets/` and runs through the existing `clang-format` / `cppcheck` / build / `ctest` CI variant. No Python changes. |
| V. Simplicity & Intentional Design | PASS | Telemetry is one periodic sampler reading `lv_mem_monitor()` and `/proc/self/statm`. No new abstractions, dependencies, or configuration framework — fixed 60 s cadence and one `lv_conf.h` flag. |

No violations → Complexity Tracking table not required.

## Project Structure

### Documentation (this feature)

```text
specs/005-fix-memory-leaks/
├── spec.md              # Feature specification (already authored)
├── plan.md              # This file
├── research.md          # Phase 0 output
├── data-model.md        # Phase 1 output
├── quickstart.md        # Phase 1 output
├── contracts/
│   └── redis-keys.md    # Phase 1 output (new Redis telemetry keys)
├── checklists/
│   └── requirements.md  # (existing) spec quality checklist
└── tasks.md             # Phase 2 output (NOT created by /speckit.plan)
```

### Source Code (repository root)

```text
src/
├── main.c                  # MODIFY: register the 60 s memory-sample timer
├── redis.c / redis.h       # MODIFY: add redis_write_mem_sample(); reuse existing connection
├── ui.c / ui.h             # AUDIT: registry-driven card add/remove path
├── memstat.c / memstat.h   # NEW: lv_mem_monitor + procfs RSS sampler, log + Redis emitter
└── widgets/
    ├── repo_status.c       # AUDIT/FIX: rebuild-on-change path
    ├── activities.c        # AUDIT/FIX: rebuild-on-change path
    ├── dev_graph.c         # AUDIT: verify series/priv lifecycle on graph_client change
    ├── status_bar.c        # AUDIT: redis-error banner reuse
    └── client_card.c       # AUDIT: card creation/teardown on registry churn

tests/
├── CMakeLists.txt          # MODIFY: register test_widget_leak
└── test_widget_leak.c      # NEW: drives widget update fns N×, asserts bounded lv_mem_monitor

lv_conf.h                   # MODIFY: enable LV_USE_MEM_MONITOR
docs/
└── ARCHITECTURE.md         # MODIFY (polish): brief "Memory telemetry" subsection
```

**Structure Decision**: Single-project C layout, unchanged. The investigation adds one new TU pair (`src/memstat.{c,h}`) and one new test (`tests/test_widget_leak.c`), with targeted edits to `main.c`, `redis.{c,h}`, `lv_conf.h`, and the widget files that the audit identifies. No new directories, no new third-party dependencies, no Python work.

## Complexity Tracking

Not required — all Constitution gates pass.

---

## Phase 0 — Research

See [research.md](research.md). Resolved items:

- LVGL heap sampling via `lv_mem_monitor()` (enabled by `LV_USE_MEM_MONITOR` in `lv_conf.h`).
- Process RSS without external tools: read `/proc/self/statm` field 2 × page size.
- Telemetry cadence: 60 s — > 1000 points across a 24 h soak; one log line per minute.
- Headless widget driving for the regression test: initialize LVGL with no display driver, call `*_update` entry points, sample `lv_mem_monitor()` before / after a 1000-iteration loop.
- Soak post-processing: an `awk` (or short Python) one-liner over the log file computes the SC-001 / SC-002 ratios; quickstart documents both.

## Phase 1 — Design & Contracts

Generated artifacts:

- **[data-model.md](data-model.md)** — `mem_sample_t` struct and the leak-source bookkeeping format used in the eventual fix report.
- **[contracts/redis-keys.md](contracts/redis-keys.md)** — new Redis keys: `kpidash:system:mem:current` (JSON, latest sample, no TTL) and `kpidash:system:mem:ring` (LPUSH/LTRIM ring of last N samples).
- **[quickstart.md](quickstart.md)** — how to enable telemetry, run the soak on the Pi 5, run the smoke variant on a developer machine, and compute the pass/fail verdict for SC-001 / SC-002.

Agent context update: `.specify/scripts/bash/update-agent-context.sh copilot` is run at the end of Phase 1 to record the new C technology touchpoints (no new languages or frameworks).

Post-design Constitution re-check: still PASS — the design adds one TU pair, one test, two Redis keys, and one `lv_conf.h` flag. No new abstractions or dependencies were introduced during Phase 1.
