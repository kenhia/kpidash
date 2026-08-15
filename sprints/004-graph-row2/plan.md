# Implementation Plan: Move Dev Graph to Row 2

**Branch**: `004-graph-row2` | **Date**: 2026-04-26 | **Spec**: [spec.md](spec.md)
**Input**: Feature specification from `/specs/004-graph-row2/spec.md`

## Summary

Move the dev graph widget from Row 1 (child of the `g_card_grid` flex container) to Row 2 (absolute-positioned child of `g_screen`). This reclaims the full 6-column card grid in Row 1, eliminating the card-offset workaround. Activities and Repo Status shift from columns 0–1/2–3 to columns 2–3/4–5 in Row 2 to make room for the graph at columns 0–1.

## Technical Context

**Language/Version**: C11 (GCC)
**Primary Dependencies**: LVGL v9.x (embedded GUI library)
**Storage**: N/A (Redis read-only from UI perspective)
**Testing**: CTest (native build), manual on-device validation for GUI
**Target Platform**: Raspberry Pi 5 (aarch64), 3840×2160 display via DRM/KMS
**Project Type**: Embedded dashboard application
**Performance Goals**: 60 fps rendering, <16ms frame budget
**Constraints**: Single-threaded LVGL event loop; all widget ops must be LVGL-thread-safe
**Scale/Scope**: Single file change (`src/ui.c`), ~15 lines modified

## Constitution Check

*GATE: Must pass before Phase 0 research. Re-check after Phase 1 design.*

| Principle | Status | Notes |
|-----------|--------|-------|
| I. Spec-Driven Development | ✅ PASS | Spec exists at `specs/004-graph-row2/spec.md` |
| II. Documentation | ✅ PASS | Layout diagram in `ui.c` header comment will be updated |
| III. TDD | ✅ PASS | Layout change is GUI-only; manual on-device validation per constitution exemption |
| IV. Code Standards Gate | ✅ PASS | clang-format, cppcheck, native build + ctest will be run |
| V. Simplicity | ✅ PASS | Net code reduction — removes offset logic, simplifies layout |

No violations. No complexity tracking needed.

## Project Structure

### Documentation (this feature)

```text
specs/004-graph-row2/
├── plan.md              # This file
├── research.md          # Phase 0 output (minimal — no unknowns)
├── data-model.md        # Phase 1 output (layout geometry only)
├── quickstart.md        # Phase 1 output
└── tasks.md             # Phase 2 output (/speckit.tasks command)
```

### Source Code (repository root)

```text
src/
├── ui.c                 # Primary change — layout positioning + graph lifecycle
├── ui.h                 # No changes
├── layout.h             # No changes (macros already support this layout)
└── widgets/
    └── dev_graph.h      # No changes (already self-sizes to UNIT_W_N(2) × UNIT_H)
```

**Structure Decision**: Single file change in existing `src/ui.c`. No new files, modules, or abstractions needed.
