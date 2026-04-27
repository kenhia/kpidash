# Feature Specification: Move Dev Graph to Row 2

**Feature Branch**: `004-graph-row2`  
**Created**: 2026-04-26  
**Status**: Draft  
**Input**: User description: "Move the dev graph widget from Row 1 to Row 2 of the dashboard layout."

## User Scenarios & Testing *(mandatory)*

### User Story 1 - Full Card Grid in Row 1 (Priority: P1)

When the dev graph is not active, Row 1 displays all 6 client card slots without any layout change from before. The card grid occupies the full 6×1 unit width with no reserved space for the graph.

**Why this priority**: The primary motivation for this change is to reclaim the full 6-column card grid so all client cards are always visible in Row 1.

**Independent Test**: Launch the dashboard with no dev graph enabled; verify all 6 card slots render correctly in Row 1.

**Acceptance Scenarios**:

1. **Given** the dashboard is running with dev graph disabled, **When** the UI renders, **Then** Row 1 shows the card grid spanning all 6 columns (6×1 units) with up to 6 client cards visible.
2. **Given** the dashboard has 6 connected clients, **When** the UI renders, **Then** all 6 client cards are visible in Row 1 without any being pushed off-screen.

---

### User Story 2 - Dev Graph in Row 2 (Priority: P1)

When the dev graph is enabled via dev command, it appears in Row 2 at columns 0–1 (2×1 units), alongside Activities (columns 2–3) and Repo Status (columns 4–5).

**Why this priority**: This is the core layout change — the graph must render correctly in its new position for the feature to be complete.

**Independent Test**: Enable the dev graph for a client; verify it renders at the correct position in Row 2 alongside Activities and Repo Status.

**Acceptance Scenarios**:

1. **Given** the dev graph is enabled for a client, **When** the UI renders, **Then** the dev graph occupies columns 0–1 of Row 2 (position COL_X(0), ROW_Y(1); size 2×1 units).
2. **Given** the dev graph is enabled, **When** the UI renders, **Then** Activities is positioned at columns 2–3 and Repo Status at columns 4–5 of Row 2.
3. **Given** the dev graph is enabled, **When** it is later disabled, **Then** the graph is destroyed and Activities and Repo Status remain at their shifted positions in Row 2.

---

### User Story 3 - No Card Offset Logic (Priority: P1)

The card index offset previously applied when the dev graph was present (to avoid the graph displacing a card slot) is removed. Cards always start at flex index 0 regardless of graph state.

**Why this priority**: This eliminates the coupling between graph visibility and card ordering, which was the source of layout complexity.

**Independent Test**: Enable the dev graph, add/remove clients, verify cards always reorder starting at index 0 with no offset applied.

**Acceptance Scenarios**:

1. **Given** the dev graph is enabled and clients are connected, **When** cards are reordered during ui_refresh, **Then** card LVGL objects are moved to indices starting at 0 (no offset).
2. **Given** the dev graph transitions from enabled to disabled, **When** ui_refresh runs, **Then** card ordering behavior is identical (index 0-based) because no offset logic exists.

---

### Edge Cases

- What happens when the dev graph is enabled but no telemetry data is available? The graph widget is created and positioned in Row 2 but shows no data points (existing behavior preserved).
- What happens when the dev graph target client changes while the graph is visible? The graph is destroyed and recreated in the same Row 2 position with the new client (existing behavior preserved).

## Requirements *(mandatory)*

### Functional Requirements

- **FR-001**: The dev graph widget MUST be created as a direct child of g_screen with absolute positioning at COL_X(0), ROW_Y(1) when enabled.
- **FR-002**: The dev graph widget MUST have size UNIT_W_N(2) × UNIT_H (2 columns × 1 row).
- **FR-003**: When the dev graph is enabled, Activities MUST be positioned at COL_X(2), ROW_Y(1) and Repo Status at COL_X(4), ROW_Y(1).
- **FR-004**: When the dev graph is disabled, Activities MUST remain at COL_X(2), ROW_Y(1) and Repo Status MUST remain at COL_X(4), ROW_Y(1).
- **FR-005**: The card index offset logic (`card_offset = g_dev_graph ? 1 : 0`) MUST be removed from ui_refresh.
- **FR-006**: The dev graph MUST no longer be created as a child of g_card_grid.
- **FR-007**: The card grid MUST always span the full 6-column width (UNIT_W_N(COLS) × UNIT_H) without sharing space with the dev graph.

## Success Criteria *(mandatory)*

### Measurable Outcomes

- **SC-001**: All 6 client card slots are visible in Row 1 regardless of dev graph state.
- **SC-002**: The dev graph, Activities, and Repo Status each occupy exactly 2 columns of Row 2 when the graph is enabled, filling all 6 columns.
- **SC-003**: Enabling and disabling the dev graph does not cause any visible layout shift in Row 1 (card grid).
- **SC-004**: Existing dev graph data feed and lifecycle (create/update/destroy) behavior is preserved without regression.

## Assumptions

- The dashboard screen resolution remains 3840×2160 with the existing 6×3 grid plus footer layout.
- Activities and Repo Status are repositioned from columns 0–1 and 2–3 to columns 2–3 and 4–5 respectively to make room for the dev graph.
- Row 3 remains empty (no widgets currently assigned).
- The footer (fortune strip + status bar) is unchanged.
- The dev graph widget's internal rendering (chart, data feed, styling) is unchanged — only its parent and position change.
