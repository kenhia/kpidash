# Data Model: Move Dev Graph to Row 2

**Feature**: 004-graph-row2 | **Date**: 2026-04-26

## Layout Geometry

No new entities or state. This feature modifies only widget positioning within the existing layout grid.

### Row 1 (before → after)

| Widget | Before | After |
|--------|--------|-------|
| Card Grid | COL_X(0), ROW_Y(0), size depends on `g_dev_graph` | COL_X(0), ROW_Y(0), always UNIT_W_N(6) × UNIT_H |
| Dev Graph | Child of `g_card_grid` at flex index 0 | **Removed from Row 1** |

### Row 2 (before → after)

| Widget | Before | After |
|--------|--------|-------|
| Dev Graph | N/A (was in Row 1) | COL_X(0), ROW_Y(1), UNIT_W_N(2) × UNIT_H, child of `g_screen` |
| Activities | COL_X(0), ROW_Y(1) | COL_X(2), ROW_Y(1) — shifted right by 2 columns |
| Repo Status | COL_X(2), ROW_Y(1) | COL_X(4), ROW_Y(1) — shifted right by 2 columns |

### Visual Grid (after)

```
Row 0:  [  Card 0  |  Card 1  |  Card 2  |  Card 3  |  Card 4  |  Card 5  ]
         col 0       col 1       col 2       col 3       col 4       col 5

Row 1:  [ Dev Graph (when enabled) | Activities          | Repo Status       ]
         col 0-1                     col 2-3               col 4-5

Row 2:  [  (empty)                                                            ]

Footer: [  Fortune strip                                                      ]
        [  Status bar                                                         ]
```

## State Changes

### Removed State

- `card_offset` local variable in `ui_refresh` — no longer needed since the dev graph is not a flex child of the card grid.

### Unchanged State

- `g_dev_graph` (lv_obj_t*) — still tracks the graph widget handle
- `g_graph_client` (char[]) — still tracks the target client hostname
- Dev graph lifecycle (create/update/destroy) — unchanged, only parent changes
