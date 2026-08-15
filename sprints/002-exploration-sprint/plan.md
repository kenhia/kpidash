# Implementation Plan: Exploration Sprint

**Branch**: `002-exploration-sprint` | **Date**: 2026-04-03 | **Spec**: [spec.md](spec.md)
**Input**: Feature specification from `/specs/002-exploration-sprint/spec.md`

## Summary

This sprint adds four interrelated capabilities to the kpidash dashboard: (1) a graphical client widget with arc gauges replacing the text-based cards, (2) Redis-driven development commands for grid overlay and text size reference, (3) iterative tuning of the Activities and Repo Status widgets, and (4) exploratory quasi-realtime GPU/VRAM graphs. The client widget redesign (P1) is the primary deliverable—it replaces `src/widgets/client_card.c` with a compound LVGL widget using stacked `lv_arc` objects for gauge visualization.

## Technical Context

**Language/Version**: C11 (dashboard), Python 3.13 (kpidash-client)
**Primary Dependencies**: LVGL 9.x (embedded GUI), hiredis (Redis C client), libcjson (JSON parsing), psutil/pynvml (Python telemetry)
**Storage**: Redis (all telemetry, health, activities, repos, commands)
**Testing**: ctest (51 C unit tests), pytest (Python clients)
**Target Platform**: Raspberry Pi 5 (aarch64, DRM/KMS framebuffer), cross-compiled from Ubuntu 22.04 x86_64
**Project Type**: Embedded dashboard application + companion CLI/daemon clients
**Performance Goals**: 30 fps LVGL render loop; Redis poll every ~1s; telemetry update every ~5s
**Constraints**: 1920×1080 display; no scrolling anywhere; single-threaded LVGL (timer-based updates); 50ms Redis read timeout
**Scale/Scope**: Up to 16 clients (6 visible in top row), 8 disks/client, 10 activities, ~20 repos

## Constitution Check

*GATE: Must pass before Phase 0 research. Re-check after Phase 1 design.*

| Principle | Status | Notes |
|-----------|--------|-------|
| I. Spec-Driven Development | ✅ PASS | Spec at `specs/002-exploration-sprint/spec.md`; all 4 user stories defined before implementation |
| II. Documentation in docs/ | ✅ PASS | Will update `docs/CLIENT-PROTOCOL.md` with new Redis command keys and OS field; `docs/ARCHITECTURE.md` updated if layout changes materially |
| III. Test-Driven Development | ✅ PASS | New C unit tests for command parsing, arc value calculations; Python tests for OS field collection. GUI rendering validated on-device (exempt per constitution) |
| IV. Code Standards Gate (C) | ✅ PASS | clang-format, cppcheck, -Wall -Wextra clean build required. Python: ruff format/check + pytest |
| V. Simplicity & Intentional Design | ✅ PASS | Arc gauges use stacked `lv_arc` (built-in LVGL widget); no custom draw functions. Dev commands use existing Redis poll loop. No speculative features |

## Project Structure

### Documentation (this feature)

```text
specs/[###-feature]/
├── plan.md              # This file (/speckit.plan command output)
├── research.md          # Phase 0 output (/speckit.plan command)
├── data-model.md        # Phase 1 output (/speckit.plan command)
├── quickstart.md        # Phase 1 output (/speckit.plan command)
├── contracts/           # Phase 1 output (/speckit.plan command)
└── tasks.md             # Phase 2 output (/speckit.tasks command - NOT created by /speckit.plan)
```

### Source Code (repository root)

```text
src/
├── main.c                    # Entry point (no changes expected)
├── redis.c / redis.h         # Redis poll — add command key parsing
├── registry.c / registry.h   # Data structs — add os_name field to client_info_t
├── config.c                  # Config loading
├── ui.c / ui.h               # Layout — modify top row for 5-6 compact widgets; add grid overlay
├── status.c / status.h       # Status messages (no changes)
├── fortune.c / fortune.h     # Fortune widget (no changes)
├── protocol.h                # Add command key macros
└── widgets/
    ├── client_card.c / .h    # REWRITE: text-based → arc gauge compound widget
    ├── activities.c / .h     # MODIFY: resize, larger text, row alignment
    ├── repo_status.c / .h    # MODIFY: dim host prefix, resize
    ├── fortune.c / .h        # (no changes)
    ├── status_bar.c / .h     # (no changes)
    ├── dev_grid.c / .h       # NEW: grid overlay widget
    └── dev_textsize.c / .h   # NEW: text size reference widget

clients/kpidash-client/
└── kpidash_client/
    ├── telemetry/
    │   └── system.py         # MODIFY: add os_name collection (platform.system + release)
    └── redis_client.py       # MODIFY: include os_name in health JSON

tests/
├── test_redis_json.c         # MODIFY: add test cases for command parsing, os_name field
└── test_config.c             # (no changes)
```

**Structure Decision**: All changes fit within the existing directory layout. Two new widget files (`dev_grid.c`, `dev_textsize.c`) follow the established `src/widgets/` pattern. No new directories needed.

## Complexity Tracking

No constitution violations. No complexity justifications needed.

## Constitution Re-Check (Post Phase 1 Design)

| Principle | Status | Post-Design Notes |
|-----------|--------|-------------------|
| I. Spec-Driven Development | ✅ PASS | Spec, plan, research, data-model, contracts all complete before implementation |
| II. Documentation in docs/ | ✅ PASS | Redis schema delta documented in `contracts/redis-schema.md`; `docs/CLIENT-PROTOCOL.md` update planned |
| III. Test-Driven Development | ✅ PASS | Testable items identified: command JSON parsing, os_name field parsing, arc value clamping. GUI arcs are on-device visual validation |
| IV. Code Standards Gate | ✅ PASS | No new tooling needed; existing clang-format + cppcheck + ruff pipeline applies to all new/modified files |
| V. Simplicity | ✅ PASS | All designs use built-in LVGL widgets (lv_arc, lv_chart, lv_obj); no custom draw functions. Dev commands reuse existing poll loop. No new threads or async patterns |

## Phase 1 Artifacts

| Artifact | Path | Status |
|----------|------|--------|
| research.md | [specs/002-exploration-sprint/research.md](research.md) | Complete |
| data-model.md | [specs/002-exploration-sprint/data-model.md](data-model.md) | Complete |
| contracts/redis-schema.md | [specs/002-exploration-sprint/contracts/redis-schema.md](contracts/redis-schema.md) | Complete |
| quickstart.md | [specs/002-exploration-sprint/quickstart.md](quickstart.md) | Complete |

## Next Step

Run `/speckit.tasks` to generate the task breakdown from this plan.
