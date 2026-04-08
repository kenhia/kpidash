# Feature Specification: Refinement & Standardization

**Feature Branch**: `003-refinement-sprint`
**Created**: 2026-04-04
**Status**: Draft
**Input**: Pre-plan from Sprint 002 (`preplan.md`)

## User Scenarios & Testing

### User Story 1 — Widget Unit System (Priority: P1) 🎯 MVP

As the dashboard developer, I want a formalized unit system so that all
widgets use consistent, predictable sizing derived from a single base unit,
eliminating magic numbers and enabling reliable layout composition across
a 3-row, 6-column grid.

**Why this priority**: Every other story depends on this. Magic numbers
scattered across `ui.c` and widgets make layout changes error-prone. The
unit system is the foundation for the unit-aware grid (US2) and any future
widget work. Currently the layout _almost_ fits 3 full-height rows but
the padding between rows and at the top leaves insufficient space — this
must be resolved as part of formalizing the unit system.

**Independent Test**: After implementation, the dashboard renders 3 rows
of unit-height widgets (client cards, activities+repo status, row 3 widgets)
with no clipping. Unit macros compute the expected values. C unit tests
verify macro arithmetic for all defined sizes.

#### Layout Calculation & Rules

The unit system must be derived from first principles for 3840×2160.
The implementation phase should review, verify, and establish the
following measurements (defaults provided as starting points — adjust
if the math demands it):

**Horizontal (3840px wide)**:
- `UNIT_W` = 632 (cell width — gap-inclusive; each cell internalizes
  half the visual gap via `CELL_PAD`)
- `CELL_PAD` = 4 (default per-side inset within each cell; adjacent
  widget content has `4+4 = 8px` visual gap)
- 6 columns: `6 × UNIT_W = 6×632 = 3792`
- Remaining horizontal: `3840 - 3792 = 48px`
- `PAD_LEFT` = `floor(48 / 2)` = 24 (left screen margin)
- `PAD_RIGHT` = `48 - PAD_LEFT` = 24 (right screen margin)
- Widget content in a 1×1 cell: `UNIT_W - 2×CELL_PAD` = 624 (unchanged
  from Sprint 002)

**Vertical (2160px tall)**:
- `UNIT_H` = 628 (cell height — gap-inclusive, same CELL_PAD logic)
- `PAD_TOP` = 0 (no top margin — maximize usable space)
- 3 rows: `3 × UNIT_H = 3×628 = 1884`
- `FOOTER_H` = `2160 - PAD_TOP - 3×UNIT_H` = 276
  (compact area below 3 unit rows for fortune strip + status bar)
- Widget content in a 1×1 cell: `UNIT_H - 2×CELL_PAD` = 620 (unchanged)

**Composite sizes** (gap-inclusive — no correction needed for multi-unit):
- `UNIT_W_N(n)` = `n × UNIT_W` (e.g., 2×1 cell = 1264, content = 1256)
- `UNIT_H_N(n)` = `n × UNIT_H`

**Key layout parameters to formalize**:

| Parameter | Description | Default |
| --------- | ----------- | ------- |
| `PAD_TOP` | Top screen margin | 0 |
| `PAD_LEFT` | Left screen margin | `floor((SCR_W - COLS×UNIT_W) / 2)` = 24 |
| `PAD_RIGHT` | Right screen margin | `SCR_W - COLS×UNIT_W - PAD_LEFT` = 24 |
| `UNIT_W` | Cell width (gap-inclusive) | 632 |
| `UNIT_H` | Cell height (gap-inclusive) | 628 |
| `CELL_PAD` | Per-side widget inset within cell | 4 |
| `FOOTER_H` | Footer area below 3 unit rows | 276 (derived) |

**Row assignment** (3 unit rows + footer):
- **Row 1**: Client card grid (6×1 units, with dev graph taking 2×1 when active)
- **Row 2**: Activities (2×1) + Repo Status (2×1) + potential future widgets
- **Row 3**: Full unit-height row — available for future widgets
- **Footer** (276px): Fortune strip + status bar

**Acceptance Scenarios**:

1. **Given** the dashboard is built with the new unit macros, **When** it
   renders on a 3840×2160 display, **Then** 3 rows of unit-height content
   render without clipping, and the footer accommodates fortune
   and status bar.
2. **Given** a developer reads `layout.h`, **When** they need a 2×1
   widget size, **Then** `UNIT_W_N(2)` produces `1264` (cell size) and
   content = `1264 - 2×CELL_PAD` = `1256`.
3. **Given** `ui.c` uses unit macros, **When** the code is searched for
   raw pixel literals (`632`, `628`, `1264`, `624`, `620`, `1256`),
   **Then** zero occurrences remain (all expressed via macros).
4. **Given** `PAD_TOP` is 0, **When** the card grid is positioned,
   **Then** row 1 starts at y=0 (flush with top of screen).
5. **Given** the unit constants and gaps, **When** all 3 rows plus footer
   are summed, **Then** the total equals exactly 2160 (no overflow,
   no unaccounted pixels).

---

### User Story 2 — Unit-Aware Dev Grid (Priority: P2)

As the dashboard developer, I want the dev grid overlay to draw lines at
unit boundaries so I can visually verify widget placement against the
formal unit grid.

**Why this priority**: Depends on US1 (unit constants). High iteration
value — the grid was heavily used during Sprint 002 for visual tuning.

**Independent Test**: Set Redis key
`kpidash:cmd:grid '{"enabled":true,"unit":true,"size":1}'`. Grid lines
appear at every unit boundary. Change `"size":0.5` — lines appear at
half-unit intervals. Omit `"unit"` field — existing pixel-based behavior
unchanged.

**Acceptance Scenarios**:

1. **Given** the grid command has `"unit":true, "size":1`, **When** the
   dashboard renders the overlay, **Then** vertical lines appear at every
   `UNIT_W` (632px) interval and horizontal lines at every `UNIT_H`
   (628px) interval, starting from `PAD_LEFT`/`PAD_TOP`, drawing only
   interior cell boundaries (gap midpoints between adjacent widgets).
2. **Given** the grid command has `"unit":true, "size":0.5`, **When**
   rendered, **Then** lines appear at half-unit intervals.
3. **Given** the grid command has no `"unit"` field, **When** rendered,
   **Then** behavior is identical to Sprint 002 (pixel-based grid).
4. **Given** the grid command has `"unit":true, "size":2`, **When**
   rendered, **Then** lines appear every 2 units, showing the 2×1 widget
   boundaries.

---

### User Story 3 — Shared Widget Styles (Priority: P3)

As the dashboard developer, I want widget colors, fonts, and styling
constants extracted into a shared header so that visual consistency is
maintained without duplicating definitions across every widget file.

**Why this priority**: Not blocking, but reduces maintenance burden.
Currently `COLOR_BG`, `COLOR_FG`, `COLOR_MUTED`, `COLOR_HEADER`, and
font pointers are copy-pasted in every widget `.c` file. A single change
(e.g., a color tweak) requires editing 8+ files.

**Independent Test**: After extraction, build succeeds. Each widget
`#include`s the shared header. `grep` confirms no widget file defines
its own `COLOR_BG` / `COLOR_FG` / `COLOR_MUTED` / `COLOR_HEADER`.

**Acceptance Scenarios**:

1. **Given** a new `widgets/common.h` exists, **When** a widget needs
   the standard background color, **Then** it uses `COLOR_BG` from the
   shared header, not a local `#define`.
2. **Given** all widgets use the shared header, **When** the muted color
   is changed in `common.h`, **Then** all widgets reflect the change
   without any other file edits.

---

### User Story 4 — Widget Rebuild Flash Fixes (Priority: P3)

As a dashboard user, I want the Repo Status and Activities widgets to
update without visible flashing so the display remains stable and
readable.

**Why this priority**: Known visual defect (kwi #22). Not blocking
functionality, but degrades the viewing experience.

**Independent Test**: With repos and activities actively refreshing, watch
the dashboard for 60 seconds. No visible flash of "All repos clean" or
blank content between updates.

**Acceptance Scenarios**:

1. **Given** repo data has not changed between polls, **When** the
   dashboard polls Redis, **Then** the repo widget does not rebuild its
   children (no flash).
2. **Given** activity data has not changed between polls, **When** the
   dashboard polls Redis, **Then** the activity widget does not rebuild
   its children.
3. **Given** repo data has changed (new commit, branch switch), **When**
   the dashboard polls, **Then** only the changed cards update; unchanged
   cards remain stable.

---

### User Story 5 — Client Card Ordering (Priority: P4)

As a dashboard user, I want to control the display order of client cards
so that my most-used machines appear in a predictable, preferred position.

**Why this priority**: Nice-to-have. Currently first-come-first-served
order. The `KPIDASH_PRIORITY_CLIENTS` env var already exists for eviction
priority but doesn't affect display order.

**Independent Test**: Set `KPIDASH_PRIORITY_CLIENTS=kubs0,rpi53`. Restart
dashboard. Verify `kubs0` appears first, `rpi53` second, regardless of
which client registers first.

**Acceptance Scenarios**:

1. **Given** `KPIDASH_PRIORITY_CLIENTS` lists hostnames in order, **When**
   cards are rendered, **Then** listed hosts appear first in the specified
   order, followed by remaining hosts in registration order.
2. **Given** a priority client has not yet reported, **When** it comes
   online, **Then** it moves to its priority position without displacing
   other priority clients.

---

### Edge Cases

- What happens when the screen resolution is not 3840×2160? (Unit macros
  should still compute correct values or a clear error should be raised.)
- What if `"unit":true` with `"size":0` is sent? (Grid should treat as
  disabled or use minimum 0.5 unit.)
- What if all repos are clean and the cache matches? (Widget should show
  "All repos clean" stably, without rebuild.)
- What if `KPIDASH_PRIORITY_CLIENTS` lists a hostname that never appears?
  (Skip it, no gap in the layout.)

## Requirements

### Functional Requirements

- **FR-001**: System MUST define base unit constants (`UNIT_W`, `UNIT_H`,
  `CELL_PAD`, `PAD_TOP`, `PAD_LEFT`, `PAD_RIGHT`, `FOOTER_H`)
  in a single header file.
- **FR-002**: System MUST provide macros to compute N×M cell dimensions:
  `UNIT_W_N(n)` = `n * UNIT_W`,
  `UNIT_H_N(n)` = `n * UNIT_H`.
- **FR-002a**: All layout parameters MUST sum to exactly the screen
  resolution with zero unaccounted pixels (horizontally and vertically).
- **FR-003**: `ui.c` MUST use unit macros for all widget sizing and
  positioning, replacing all raw pixel literals.
- **FR-004**: Dev grid MUST support a `"unit"` boolean field in the
  Redis command JSON, switching between pixel and unit-based grid modes.
- **FR-005**: Dev grid MUST support fractional unit sizes (0.5, 1, 2)
  when in unit mode.
- **FR-006**: Widget color palette (`WS_COLOR_BG`, `WS_COLOR_FG`,
  `WS_COLOR_MUTED`, `WS_COLOR_HEADER`, `WS_COLOR_GREEN`, `WS_COLOR_BLUE`,
  and remaining accent colors) MUST be defined once in `widgets/common.h`.
- **FR-007**: Widget font constants (`FONT_HDR`, `FONT_BODY`, standard
  sizes) MUST be defined once in `widgets/common.h`.
- **FR-008**: Repo Status widget MUST NOT rebuild children when the
  underlying data has not changed between polls.
- **FR-009**: Activities widget MUST NOT rebuild children when the
  underlying data has not changed between polls.
- **FR-010**: Client card display order MUST respect the
  `KPIDASH_PRIORITY_CLIENTS` ordering when set.
- **FR-011**: Unit system constants MUST be documented in
  `docs/ARCHITECTURE.md`.
- **FR-012**: Dev grid `"unit"` field MUST be documented in
  `docs/CLIENT-PROTOCOL.md`.

### Key Entities

- **Unit**: The base layout quantum (624×620 at 3840×2160). All widget
  sizes are expressed as integer or half-integer multiples of this unit.
- **Widget Style**: A shared set of colors, fonts, and spacing values
  used consistently across all dashboard widgets.
- **Priority List**: An ordered set of hostnames that determines client
  card display order.

## Success Criteria

### Measurable Outcomes

- **SC-001**: Zero raw pixel literals for widget sizing remain in `ui.c`
  after unit macro adoption.
- **SC-001a**: Layout constants sum to exactly 3840 horizontally and 2160
  vertically with zero unaccounted pixels.
- **SC-001b**: Three full unit-height rows render without clipping on
  3840×2160.
- **SC-002**: Unit grid overlay renders correctly at sizes 0.5, 1, and 2,
  verified by visual comparison against known widget boundaries.
- **SC-003**: Color/font `#define` duplication across widget files reduced
  to zero (single source in `common.h`).
- **SC-004**: Repo Status widget shows zero visible flashes during 60
  seconds of observation with stable data.
- **SC-005**: Activities widget shows zero visible flashes during 60
  seconds of observation with stable data.
- **SC-006**: All existing tests (133 C + 52 Python) continue to pass.
- **SC-007**: New unit arithmetic tests added and passing.
- **SC-008**: Priority client ordering matches configured order within
  1 poll cycle of the client coming online.

## Assumptions

- Target display remains 3840×2160. Resolution-independence (e.g., 1920×1080
  support) is deferred to a future sprint.
- The base unit dimensions (624×620) from Sprint 002 are correct and
  do not need further tuning.
- `KPIDASH_PRIORITY_CLIENTS` already exists in `config.c`; this sprint
  extends its use to display ordering (currently only affects eviction).
- The `kwi #22` flash bug root cause is rebuilding unchanged children;
  the fix is data-level caching before calling widget update functions.
- `clang-format` and `cppcheck` remain the C quality gates; `ruff` for Python.
