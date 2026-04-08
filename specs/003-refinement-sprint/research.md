# Research: Refinement & Standardization

**Sprint**: 003-refinement-sprint | **Date**: 2026-04-04

## R1: Cell-Based Unit System (replaces gap-based model)

**Decision**: Gap-inclusive cells with `CELL_PAD = 4` per side

**Rationale**: Instead of separate `UNIT_W`/`H_GAP` constants requiring
correction formulas for multi-unit sizing (`n*W + (n-1)*GAP`), each cell
internalizes half the visual gap. This makes `UNIT_W_N(n) = n * UNIT_W`
(no correction), grid overlay trivial (`PAD_LEFT + N*UNIT_W`), and
fractional units natural. Widgets apply `CELL_PAD` as internal padding;
the 8px visual gap between adjacent content is preserved (4+4).

**Constants**:
- `UNIT_W` = 632 (cell width = 624 content + 2×4 pad)
- `UNIT_H` = 628 (cell height = 620 content + 2×4 pad)
- `CELL_PAD` = 4 (default per-side widget inset)
- `PAD_LEFT` = `PAD_RIGHT` = 24
- `FOOTER_H` = 276

**Current code reference**: `ui.c:114` sets `pad_all(8)` on screen;
`mid_y = card_area_h + 16` positions activities row with effective 8px gap.
Both replaced by cell-based positioning.

**Vertical accounting (3 cell rows + footer)**:
```
PAD_TOP    =    0
Row 1      =  628  (UNIT_H)    — client cards
Row 2      =  628  (UNIT_H)    — activities + repo status
Row 3      =  628  (UNIT_H)    — future widgets
FOOTER_H   =  276  (2160 - 3×628)
Total      = 0 + 628 + 628 + 628 + 276 = 2160  ✓
```
FOOTER_H holds fortune (216px) + status bar (40px) + 20px spacing.

**Alternatives considered**:
- Separate `UNIT_W` + `H_GAP` (old model): Multi-unit formulas require
  gap correction; grid overlay needs `UNIT_W + H_GAP` spacing; confusing
  when mixing cell vs content sizes.
- `CELL_PAD = 2` (4px visual gap): Too tight between widgets at 3840px.

## R2: Row 3 — Full Unit Row

**Decision**: Row 3 is a full unit-height row (628px cell). The area below
all three rows is a compact footer strip for fortune + status bar.

**Rationale**: The spec calls for a "3-row, 6-column grid" and acceptance
scenario 1 requires "3 rows of unit-height content render without clipping."
With only 2 unit rows the remaining space is 904px — far more than fortune
(216px) + status bar (40px) need, and 648px of that is wasted. A third full
unit row reclaims that space for future widgets (planned in upcoming sprints).

**Vertical accounting (3 cell rows + footer)**:
```
PAD_TOP    =    0
Row 1      =  628  (UNIT_H)    — client cards
Row 2      =  628  (UNIT_H)    — activities + repo status
Row 3      =  628  (UNIT_H)    — future widgets
FOOTER_H   =  276  (2160 - 3×628)
Total      = 0 + 628 + 628 + 628 + 276 = 2160 ✓
```

FOOTER_H (276px) comfortably fits fortune (216px) + status bar (40px) with
20px remaining for spacing.

**Current row 3 usage**: Fortune strip + status bar live in the footer
below row 3 for now. Row 3 is available for new widget content in future
sprints. During this sprint, if no row-3 widgets are added yet, the row-3
area may remain empty or the fortune/status can be repositioned into it —
either way the unit grid accounts for it.

**Alternatives considered**:
- 2 unit rows + 904px gutter: Wastes 648px (more than a full unit height).
  Doesn't match spec's 3-row requirement.
- Variable-height row 3: Over-complicates the unit system with a special case.

Note: The card_area_h is currently 640 (not 628). This needs to change to
UNIT_H=628 as part of the unit system adoption. The extra 12px was informal
padding that the cell-based unit system eliminates.

## R3: Widget Change Detection (Flash Fix)

**Decision**: Static cache with `memcmp` comparison before rebuild

**Rationale**: Repo Status already implements this pattern successfully
(`repo_status.c:216-245`). Activities does not — it destroys and rebuilds
all children every poll cycle, causing visible flashing.

**Implementation approach**:
1. **Activities**: Add static `s_cache[MAX_ACTIVITIES]` and `s_cache_count`.
   Before rebuilding, compare incoming data with cache via `memcmp`. Skip
   rebuild if data unchanged. Only the elapsed-time timer updates
   independently (already working).
2. **Repo Status**: Already cached — verify the existing mechanism works
   correctly under the new unit layout. No changes expected.

**Alternatives considered**:
- JSON string hashing: More complex, requires serialization. The struct
  `memcmp` approach is simpler and already proven in repo_status.
- Generation counter in Redis payload: Requires client-side changes.
  Unnecessary when data comparison is sufficient.

**Data flow**:
- `redis_get_activities()` → `activity_t *list` → `activities_widget_update()`
- `redis_get_repos()` → `repo_entry_t *list` → `repo_status_widget_update()`

Both use struct arrays, making `memcmp` straightforward.

## R4: Client Card Priority Ordering

**Decision**: Sort in `ui_refresh()` after snapshot, before add/update cards

**Rationale**: The registry already stores priority client data via
`registry_set_priority_clients()` (parsed from `KPIDASH_PRIORITY_CLIENTS`
env var in `config.c:41-51`). The simplest approach is to sort the snapshot
array in `ui_refresh()` before processing cards, placing priority clients
first in their configured order.

**Implementation approach**:
1. After `registry_snapshot()` in `ui_refresh()`, call a sort function
   that partitions the array: priority clients first (in config order),
   then remaining clients in registration order.
2. The existing `add_card()` / `remove_absent_cards()` logic handles
   card creation/removal — ordering just needs the snapshot sorted.
3. Need to also reorder existing cards' positions in the flex container
   when priority list changes, or when a priority client comes online.

**Alternatives considered**:
- Sort inside registry_snapshot(): Couples registry to display concerns.
- Maintain a separate sorted array: Extra state to synchronize.
- Reorder LVGL children directly: `lv_obj_move_to_index()` — possible
  but fragile with dynamic add/remove. Sorting the data array is cleaner.

**Config storage**: `cfg->priority_clients[PRIORITY_CLIENTS_MAX][HOSTNAME_LEN]`
with `cfg->priority_client_count`. Max 16 clients.

## R5: Color and Font Duplication

**Decision**: Create `src/widgets/common.h` with shared palette and font pointers

**Rationale**: 6 widget files define local `COLOR_*` and `FONT_*` macros.
The core Catppuccin Mocha palette is consistent across files:
- BG: 0x1E1E2E (5 files), 0x181825 (fortune — intentionally different)
- FG: 0xCDD6F4 (4 files)
- Muted: 0x6C7086 (4 files)
- Header: 0xF5C2E7 (2 files)

Fortune uses a darker BG (0x181825) — this is intentional for visual
differentiation and will be kept as a widget-specific override.

**What goes in common.h**:
- Core palette: `WS_COLOR_BG`, `WS_COLOR_FG`, `WS_COLOR_MUTED`, `WS_COLOR_HEADER`
- Accent colors: `WS_COLOR_GREEN`, `WS_COLOR_BLUE`, `WS_COLOR_RED`,
  `WS_COLOR_ORANGE`, `WS_COLOR_MAUVE`, `WS_COLOR_YELLOW`
- Font pointers: `WS_FONT_HDR` (bold_20), `WS_FONT_BODY` (montserrat_20),
  `WS_FONT_SMALL` (montserrat_14/16)

**Prefix convention**: `WS_` (widget style) to avoid clashes with LVGL's
`LV_` namespace and any other library macros.

**Alternatives considered**:
- Separate `colors.h` and `fonts.h`: Over-splitting for ~20 macros total.
- `theme.h` at `src/` level: Widget styles are widget-internal; keeping
  the header in `src/widgets/` maintains locality.

## R6: Horizontal Layout Verification

**Derived from spec, verified against current code**:

```
PAD_LEFT   =   24
Cell 1     =  632  (UNIT_W = 624 content + 2×4 pad)
Cell 2     =  632
Cell 3     =  632
Cell 4     =  632
Cell 5     =  632
Cell 6     =  632
PAD_RIGHT  =   24
Total      = 24 + 6×632 + 24 = 24 + 3792 + 24 = 3840 ✓
```

Visual gap between adjacent widget content: `CELL_PAD + CELL_PAD = 4 + 4 = 8px`.

Current code uses `pad_all(8)` giving 8px margins and `scr_w - 16` widths.
The cell-based unit system changes PAD_LEFT/RIGHT from 8 to 24.

## R7: LVGL Dev Grid Unit Mode

**Decision**: Extend existing grid drawing to support unit-based intervals

**Current implementation** (`dev_grid.c`): Draws pixel-based grid lines
using LVGL canvas or line objects at fixed pixel intervals controlled by
the `"size"` field in the Redis command JSON.

**New behavior**: When `"unit":true` is present in the grid command:
- `"size":1` → lines at every UNIT_W/UNIT_H interval, starting from
  PAD_LEFT/PAD_TOP, drawing interior cell boundaries only (gap midpoints
  between adjacent widgets)
- `"size":0.5` → lines at half-unit intervals
- `"size":2` → lines at double-unit intervals
- No `"unit"` field → existing pixel-based behavior unchanged

**Implementation**: Read the `"unit"` bool from JSON. If true, iterate
from `PAD_LEFT + N*UNIT_W*unit_size` for interior N values (skip screen
edges). Include `layout.h` for the constants.
