# Data Model: Refinement & Standardization

**Sprint**: 003-refinement-sprint | **Date**: 2026-04-04

## Entities

### 1. Layout Constants (layout.h)

Compile-time constants defining the grid system. No runtime state.

| Field | Type | Value | Description |
|-------|------|-------|-------------|
| `SCR_W` | `int32_t` | 3840 | Screen width |
| `SCR_H` | `int32_t` | 2160 | Screen height |
| `UNIT_W` | `int32_t` | 632 | Cell width (gap-inclusive) |
| `UNIT_H` | `int32_t` | 628 | Cell height (gap-inclusive) |
| `CELL_PAD` | `int32_t` | 4 | Default per-side widget inset within cell |
| `PAD_TOP` | `int32_t` | 0 | Top screen margin |
| `PAD_LEFT` | `int32_t` | 24 | Left screen margin (derived) |
| `PAD_RIGHT` | `int32_t` | 24 | Right screen margin (derived) |
| `FOOTER_H` | `int32_t` | 276 | Footer height below 3 cell rows (derived) |
| `COLS` | `int` | 6 | Number of columns |
| `ROWS` | `int` | 3 | Number of unit-height rows |

**Macros**:
- `UNIT_W_N(n)` → `((n) * UNIT_W)` (gap-inclusive: no correction needed)
- `UNIT_H_N(n)` → `((n) * UNIT_H)`
- `ROW_Y(r)` → `(PAD_TOP + (r) * UNIT_H)` (0-indexed row → y position)
- `COL_X(c)` → `(PAD_LEFT + (c) * UNIT_W)` (0-indexed column → x position)

**Validation rules**:
- `PAD_LEFT + COLS * UNIT_W + PAD_RIGHT == SCR_W`
- `PAD_TOP + ROWS * UNIT_H + FOOTER_H == SCR_H`

**Static assertions** (enforced at compile time):
```c
_Static_assert(PAD_LEFT + COLS * UNIT_W + PAD_RIGHT == SCR_W,
               "Horizontal layout must sum to SCR_W");
_Static_assert(PAD_TOP + ROWS * UNIT_H + FOOTER_H == SCR_H,
               "Vertical layout must sum to SCR_H");
```

### 2. Widget Style Constants (widgets/common.h)

Shared color and font definitions. No runtime state.

**Colors** (Catppuccin Mocha palette):

| Field | Type | Value | Description |
|-------|------|-------|-------------|
| `WS_COLOR_BG` | hex | 0x1E1E2E | Widget background (Base) |
| `WS_COLOR_FG` | hex | 0xCDD6F4 | Primary text (Text) |
| `WS_COLOR_MUTED` | hex | 0x6C7086 | Secondary text (Overlay0) |
| `WS_COLOR_HEADER` | hex | 0xF5C2E7 | Header accent (Pink) |
| `WS_COLOR_GREEN` | hex | 0xA6E3A1 | Active/success (Green) |
| `WS_COLOR_BLUE` | hex | 0x89B4FA | Done/info (Blue) |
| `WS_COLOR_RED` | hex | 0xF38BA8 | Error/alert (Red) |
| `WS_COLOR_ORANGE` | hex | 0xFAB387 | Warning/warm (Peach) |
| `WS_COLOR_MAUVE` | hex | 0xCBA6F7 | Accent (Mauve) |
| `WS_COLOR_YELLOW` | hex | 0xF9E2AF | Caution (Yellow) |
| `WS_COLOR_SURFACE0` | hex | 0x313244 | Card backgrounds (Surface0) |
| `WS_COLOR_SURFACE1` | hex | 0x45475A | Borders/separators (Surface1) |

**Fonts**:

| Field | Type | Value | Description |
|-------|------|-------|-------------|
| `WS_FONT_HDR` | `lv_font_t *` | `&lv_font_montserrat_bold_20` | Header/title font |
| `WS_FONT_BODY` | `lv_font_t *` | `&lv_font_montserrat_20` | Body text font |
| `WS_FONT_SMALL` | `lv_font_t *` | `&lv_font_montserrat_16` | Small text font |
| `WS_FONT_TINY` | `lv_font_t *` | `&lv_font_montserrat_14` | Tiny text (status bar) |
| `WS_FONT_LARGE` | `lv_font_t *` | `&lv_font_montserrat_bold_24` | Large headers |

### 3. Activity Cache (activities.c — internal)

Static struct for change detection. Not a new type — reuses `activity_t`.

| Field | Type | Description |
|-------|------|-------------|
| `s_cache` | `activity_t[MAX_ACTIVITIES]` | Last-rendered activity data |
| `s_cache_count` | `int` | Number of cached entries (-1 = uninitialized) |

**Comparison**: `memcmp` of `activity_t` array up to field offset before
volatile fields (e.g., exclude any timestamp that changes every second).
If the active activity's elapsed time is the only change, skip rebuild
and let the existing timer handle display updates.

### 4. Dev Grid Command (extended)

Existing `grid_cmd_t` extended with unit mode fields:

| Field | Type | Default | Description |
|-------|------|---------|-------------|
| `enabled` | `bool` | false | Grid overlay on/off |
| `size` | `int` | 100 | Pixel-based grid spacing (existing) |
| `unit` | `bool` | false | NEW: Unit-based grid mode |
| `unit_size` | `float` | 1.0 | NEW: Unit multiplier (0.5, 1, 2) |

**State transitions**: When `unit=true`, the `size` field is ignored and
grid lines are drawn at interior cell boundaries: `PAD_LEFT + N * unit_size * UNIT_W`
for vertical lines and `PAD_TOP + N * unit_size * UNIT_H` for horizontal
lines (skipping screen edges). These lines fall at gap midpoints between
adjacent widget content areas.

### 5. Priority-Sorted Snapshot (ui.c — internal)

No new entity. The existing `client_info_t` array from `registry_snapshot()`
is sorted in-place in `ui_refresh()` before card processing.

**Sort key**: Index in `priority_clients[]` config (lower = earlier).
Non-priority clients sort after all priority clients, preserving their
registration order.

## Relationships

```
layout.h ──────────────► ui.c (positioning, sizing; widgets apply CELL_PAD)
    │                    ├── widgets/client_card.c (card size = UNIT_W × UNIT_H)
    │                    ├── widgets/activities.c (size = UNIT_W_N(2) × UNIT_H)
    │                    ├── widgets/repo_status.c (size = UNIT_W_N(2) × UNIT_H)
    │                    └── widgets/dev_grid.c (unit-based grid at cell boundaries)
    │
    └──────────────────► tests/test_layout.c (macro arithmetic validation)

common.h ──────────────► all widget .c files (colors, fonts)

config.h ──────────────► ui.c (priority_clients for sort)
    └──────────────────► registry.c (existing priority check)
```
