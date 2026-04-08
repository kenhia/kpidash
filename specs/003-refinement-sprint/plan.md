# Implementation Plan: Refinement & Standardization

**Branch**: `003-refinement-sprint` | **Date**: 2026-04-04 | **Spec**: [spec.md](spec.md)
**Input**: Feature specification from `/specs/003-refinement-sprint/spec.md`

## Summary

Formalize the implicit widget layout into an explicit unit system
(`layout.h`), extract duplicated widget styles into `widgets/common.h`,
add unit-aware dev grid mode, fix widget rebuild flashing, and implement
priority-based client card ordering. The primary technical approach is
C preprocessor macros for compile-time layout arithmetic, data-level
change detection to suppress unnecessary widget rebuilds, and a sort
pass in `ui_refresh()` for card ordering.

## Technical Context

**Language/Version**: C11  
**Primary Dependencies**: LVGL 9.2.2 (vendored), hiredis, libcjson, libdrm, libpng  
**Storage**: Redis 7.x (message bus, not persistent data store)  
**Testing**: ctest (C unit tests: test_config, test_redis_json), pytest (Python client tests)  
**Target Platform**: Raspberry Pi 5, Debian 13, aarch64, DRM/KMS 3840×2160 @ 30 Hz  
**Project Type**: Embedded dashboard (single-purpose kiosk)  
**Performance Goals**: 30 fps LVGL render, <1s widget refresh latency  
**Constraints**: Single 3840×2160 display, 8 GB RAM, no GPU acceleration (software rendering)  
**Scale/Scope**: Up to 16 simultaneous clients, ~20 source files, 10 widget modules

## Constitution Check

*GATE: Must pass before Phase 0 research. Re-check after Phase 1 design.*

| Principle | Status | Notes |
|-----------|--------|-------|
| I. Spec-Driven Development | ✅ PASS | Spec exists at `specs/003-refinement-sprint/spec.md` with acceptance criteria |
| II. Documentation in docs/ | ✅ PASS | FR-011 requires ARCHITECTURE.md update; FR-012 requires CLIENT-PROTOCOL.md update |
| III. TDD | ✅ PASS | FR-002/SC-007 require unit arithmetic tests; widget/GUI code exempt per constitution |
| IV. Code Standards Gate | ✅ PASS | clang-format, cppcheck, ctest, cross-compile gates active; ruff for Python |
| V. Simplicity & Intentional Design | ✅ PASS | Macros are simplest approach for compile-time layout; no over-abstraction |

## Project Structure

### Documentation (this feature)

```text
specs/003-refinement-sprint/
├── plan.md              # This file (/speckit.plan command output)
├── research.md          # Phase 0 output (/speckit.plan command)
├── data-model.md        # Phase 1 output (/speckit.plan command)
├── quickstart.md        # Phase 1 output (/speckit.plan command)
└── tasks.md             # Phase 2 output (/speckit.tasks command - NOT created by /speckit.plan)
```

### Source Code (repository root)

```text
src/
├── layout.h             # NEW: Unit system constants and macros (FR-001, FR-002)
├── config.c/h           # Existing: Priority client parsing (no changes needed)
├── main.c               # Existing entry point (no changes expected)
├── protocol.h           # Existing protocol definitions
├── redis.c/h            # Existing Redis communication
├── registry.c/h         # Existing: Priority check API (no changes needed)
├── fortune.c/h          # Existing fortune fetch
├── status.c/h           # Existing status handling
├── ui.c/h               # MODIFY: Replace magic numbers with unit macros (FR-003)
└── widgets/
    ├── common.h         # NEW: Shared colors, fonts, style constants (FR-006, FR-007)
    ├── activities.c/h   # MODIFY: Use common.h, add change detection (FR-009)
    ├── client_card.c/h  # MODIFY: Use common.h
    ├── dev_graph.c/h    # MODIFY: Use common.h
    ├── dev_grid.c/h     # MODIFY: Add unit-aware grid mode (FR-004, FR-005)
    ├── dev_textsize.c/h # MODIFY: Use common.h
    ├── fortune.c/h      # MODIFY: Use common.h
    ├── repo_status.c/h  # MODIFY: Use common.h, add change detection (FR-008)
    └── status_bar.c/h   # MODIFY: Use common.h

tests/
├── test_config.c        # Existing (21 tests)
├── test_redis_json.c    # Existing (112 tests)
└── test_layout.c        # NEW: Unit arithmetic macro tests (SC-007)

docs/
├── ARCHITECTURE.md      # MODIFY: Document unit system (FR-011)
└── CLIENT-PROTOCOL.md   # MODIFY: Document grid "unit" field (FR-012)
```

**Structure Decision**: Single-project C layout. New files are `src/layout.h`
(constants/macros), `src/widgets/common.h` (shared styles), and
`tests/test_layout.c` (unit arithmetic tests). No new directories needed.

## Complexity Tracking

No constitution violations. No justifications required.
