# Research: Move Dev Graph to Row 2

**Feature**: 004-graph-row2 | **Date**: 2026-04-26

## Findings

No NEEDS CLARIFICATION items — all technical context is known from the existing codebase.

### R1: Dev Graph Parent & Positioning Strategy

**Decision**: Change `dev_graph_create` parent from `g_card_grid` (flex child) to `g_screen` (absolute child), then set position via `lv_obj_set_pos(COL_X(0), ROW_Y(1))`.

**Rationale**: The dev graph widget already self-sizes to `UNIT_W_N(2) × UNIT_H` internally (`dev_graph.c:53`). As a direct child of `g_screen` with absolute positioning, it will occupy exactly columns 0–1 of Row 2, matching the existing Activities and Repo Status placement pattern. No changes to `dev_graph.c` are needed.

**Alternatives considered**:
- Creating a Row 2 flex container: rejected — adds unnecessary complexity. Activities and Repo Status are already absolute-positioned children of `g_screen`; the graph should follow the same pattern.

### R2: Card Offset Removal

**Decision**: Remove the `card_offset = g_dev_graph ? 1 : 0` logic and always use index `i` (0-based) in `lv_obj_move_to_index`.

**Rationale**: The offset existed because the dev graph was a flex child of `g_card_grid` occupying index 0, pushing cards to index 1+. With the graph moved to `g_screen`, the card grid has no foreign children, so cards always start at index 0.

**Alternatives considered**: None — this is the only correct approach once the graph leaves the card grid.

### R3: Activities & Repo Status Repositioning

**Decision**: Change initial positions in `ui_init` from `COL_X(0)`/`COL_X(2)` to `COL_X(2)`/`COL_X(4)`. These positions are permanent regardless of dev graph state (FR-003, FR-004).

**Rationale**: With the dev graph at columns 0–1, Activities and Repo Status shift right to fill the remaining 4 columns (2–3 and 4–5). The spec explicitly states they stay at these positions even when the graph is disabled, simplifying the layout to a static arrangement.

**Alternatives considered**:
- Dynamic repositioning (shift back to 0–1/2–3 when graph disabled): rejected by spec (FR-004) — Activities and Repo Status always stay at columns 2–3/4–5.
