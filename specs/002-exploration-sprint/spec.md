# Feature Specification: Exploration Sprint

**Feature Branch**: `002-exploration-sprint`  
**Created**: 2026-04-03  
**Status**: Draft  
**Input**: Exploration sprint covering development commands, widget tuning, quasi-realtime graphs, and client widget redesign

## Sprint Nature

This sprint is a mix of well-defined features and iterative exploration. Some items (client widget redesign, development commands) have clear requirements and acceptance criteria. Others (widget tuning, quasi-realtime graphs) are exploratory — their outcomes will be discovered through iteration, and this spec will be updated as learning occurs.

- **Defined features**: Development Commands, Client Widget Redesign
- **Exploratory/iterative**: Widget Tuning, Quasi-Realtime Graphs

## User Scenarios & Testing *(mandatory)*

### User Story 1 — Client Widget Redesign (Priority: P1)

As the dashboard user, I want each client machine represented by a compact graphical widget with arc gauges, health indicator, hostname, and disk bars so I can assess system health at a glance without reading dense text.

**Why this priority**: The current text-based client cards are the primary dashboard element. Replacing them with a graphical widget transforms the core user experience and unblocks the layout for fitting 5–6 clients across the top row.

**Independent Test**: Deploy the dashboard with at least two clients reporting telemetry; visually confirm that each client widget renders arc gauges, hostname, OS, uptime, and disk bars matching the reported data.

**Acceptance Scenarios**:

1. **Given** a client is online and reporting full telemetry (CPU, RAM, GPU, disks), **When** the dashboard renders, **Then** the client widget displays:
   - A center health circle (green) indicating active status
   - Left half-ring arcs showing GPU VRAM (outer) and GPU usage (inner) fill proportional to reported percentages
   - Right half-ring arcs showing RAM (outer) and CPU (inner) fill proportional to reported percentages
   - CPU inner arc shows top-core usage drawn first, with average usage layered on top partially covering it
   - GPU text box (left of arcs) showing VRAM used/total and usage percentage
   - CPU text box (right of arcs) showing RAM used/total and CPU avg/top-core percentages
   - Hostname bar with OS name below
   - Uptime label in upper-right area of hostname region, formatted as "up Xd Yh"
   - Up to 6 disk bars with fill color indicating usage tier

2. **Given** a client has no discrete GPU, **When** the dashboard renders, **Then** the left-side arcs display as solid gray with no GPU text shown.

3. **Given** a client goes offline (health key expires), **When** the dashboard polls, **Then** the center health circle turns red.

4. **Given** a client reports 4 disks, **When** the dashboard renders, **Then** 4 disk bars appear, each with used amount (left), total amount (right), drive label (centered), and fill color based on usage: green (≤60%), orange (60–75%), red (>75%).

5. **Given** a client reports 8 disks, **When** the dashboard renders, **Then** only 6 disk bars appear (capped), with no scrolling.

6. **Given** the display is 1920×1080, **When** 6 clients are online, **Then** all 6 client widgets fit across the top row without overlap or truncation.

---

### User Story 2 — Development Commands (Priority: P2)

As a developer iterating on the dashboard layout, I want to send commands via Redis to toggle a grid overlay and display a text size reference widget so I can accurately position and size elements without modifying code.

**Why this priority**: Development commands accelerate all other work in this sprint. Grid overlay and text size reference are prerequisites for efficient widget tuning and layout iteration.

**Independent Test**: With the dashboard running, publish a grid-enable command to Redis; visually confirm the grid overlay appears. Publish a text-size command; confirm the reference widget appears.

**Acceptance Scenarios**:

1. **Given** the dashboard is running normally, **When** a grid-enable command is published to Redis with a grid size of 50 pixels, **Then** a translucent grid overlay appears on the dashboard with lines spaced 50 pixels apart.

2. **Given** the grid overlay is active, **When** a grid-disable command is published, **Then** the grid overlay disappears and the dashboard returns to normal display.

3. **Given** the dashboard is running, **When** a text-size-show command is published, **Then** a widget appears displaying sample text at multiple sizes for visual comparison.

4. **Given** the text size widget is visible, **When** a text-size-hide command is published, **Then** the widget disappears.

5. **Given** the dashboard is running, **When** a grid-enable command specifies a grid size of 100 pixels, **Then** the grid overlay uses 100-pixel spacing.

---

### User Story 3 — Widget Tuning (Priority: P3, Exploratory)

As the dashboard user, I want the Activities and Repo Status widgets to use less screen space, have larger readable text, and make it easy to match data to rows so the dashboard is comfortable to read from a distance.

**Why this priority**: These widgets already work but are hard to read. Tuning them improves daily usability and is relatively low-risk since the widgets exist.

**Independent Test**: Compare before/after screenshots of the Activities and Repo Status widgets; confirm text is larger, rows are more readable, and right-side data visually aligns with its row.

**Acceptance Scenarios**:

1. **Given** the Activities widget is displayed with multiple entries, **When** the user reads it from normal viewing distance, **Then** the text is legible and each row's right-side data (elapsed time) clearly corresponds to its activity name.

2. **Given** the Repo Status widget displays "host/repo" entries, **When** the user reads it, **Then** the "host/" portion appears dimmer than the "repo" name, making the repository name stand out.

3. **Given** the Activities or Repo Status widgets are displayed, **When** more entries exist than fit in the available space, **Then** entries are capped (no scrolling) and the most relevant entries are shown.

---

### User Story 4 — Quasi-Realtime Graphs (Priority: P4, Exploratory)

As a developer, I want to view time-series graphs of GPU usage and VRAM consumption on the dashboard (similar to nvtop) so I can monitor GPU behavior during development without switching to a separate terminal.

**Why this priority**: This is exploratory — the primary goal is to determine whether graph display is feasible and how it integrates with the existing layout. The implementation approach (reserved screen space vs. page/mode switching) will be discovered through iteration.

**Independent Test**: With at least one GPU-equipped client reporting telemetry, confirm that GPU usage and VRAM graphs render and update over time.

**Acceptance Scenarios**:

1. **Given** a GPU-equipped client is reporting telemetry, **When** the graph view is active, **Then** the dashboard displays time-series graphs showing GPU usage and VRAM consumption updating in near-real-time.

2. **Given** the graph view is active and 6 clients are displayed, **When** space is constrained, **Then** the two lowest-priority client widgets are suppressed to make room for the graphs.

3. **Given** the graph view is not active, **When** the user enables it (mechanism TBD during exploration), **Then** the layout transitions to accommodate graphs without requiring a restart.

---

### Edge Cases

- Client reports zero disks: disk bar area is empty, widget height adjusts or shows minimal footprint
- Client reports GPU with 0% usage and 0 MB VRAM used: arcs show as empty (background track visible), text shows "0 MB / X GB" and "0.0%"
- Two grid-enable commands in sequence: second command updates the grid size, no duplicate overlays
- Grid size of 0 or negative: command is ignored, grid remains unchanged
- Client hostname is very long (>20 characters): hostname text truncates with ellipsis within the hostname bar
- Uptime less than 1 hour: displays as "up 0d 0h" or "up <1h" (to be determined during iteration)
- All 6 disk bar slots filled with 100% usage: all bars render red, text remains readable

## Requirements *(mandatory)*

### Functional Requirements

#### Development Commands

- **FR-001**: The dashboard MUST accept a grid-overlay-enable command via Redis, including a configurable grid size parameter (in pixels)
- **FR-002**: The dashboard MUST accept a grid-overlay-disable command via Redis to remove the grid
- **FR-003**: The dashboard MUST accept a text-size-widget-show command via Redis to display a reference widget with text rendered at multiple sizes
- **FR-004**: The dashboard MUST accept a text-size-widget-hide command via Redis to remove the text size widget
- **FR-005**: Development commands MUST NOT interfere with normal dashboard operation when not active

#### Client Widget Redesign

- **FR-010**: Each client widget MUST display a center health circle that is green when the client is online and red when offline
- **FR-011**: Each client widget MUST display left-side half-ring arcs (lower hemisphere) for GPU VRAM (outer) and GPU usage (inner)
- **FR-012**: Each client widget MUST display right-side half-ring arcs (lower hemisphere) for RAM (outer) and CPU (inner)
- **FR-013**: The CPU inner arc MUST be split: top-core percentage drawn first, then CPU average layered on top partially covering the top-core arc
- **FR-014**: A visible gap MUST exist between the left and right arc halves (achieved via offset centers or trimming degrees from arc ends)
- **FR-015**: Arc thickness MUST be approximately: outer ring 10% of radius, inner ring 10% of radius, gap between rings 3% of radius
- **FR-016**: A GPU text box MUST appear to the left of the arcs showing VRAM used/total (e.g., "431 MB / 16.0 GB") and usage percentage (e.g., "12.4%")
- **FR-017**: A CPU text box MUST appear to the right of the arcs showing RAM used/total (e.g., "18.3 GB / 125.6 GB") and CPU avg/top-core (e.g., "2.5% / 27.4%")
- **FR-018**: A hostname bar MUST appear below the arcs displaying the hostname in large (bold) text, with the OS name in smaller text underneath
- **FR-019**: An uptime label MUST appear in the upper-right area of the hostname region, formatted as "up Xd Yh"
- **FR-020**: Disk bars MUST appear below the hostname area, one per reported disk, capped at 6 (no scrolling)
- **FR-021**: Each disk bar MUST display used amount (left-justified), total amount (right-justified), and drive label (centered)
- **FR-022**: Disk bar fill color MUST indicate usage tier: green for ≤60%, orange for 60–75%, red for >75%. The entire bar fill is one solid color based on the usage percentage
- **FR-023**: When a client has no GPU, left-side arcs MUST display as solid gray with no GPU text
- **FR-024**: The dashboard top row MUST accommodate approximately 5–6 client widgets across the full width
- **FR-025**: The OS name field MUST use the value from `platform.system() + " " + platform.release()` (cross-platform, requires client-side change)

#### Widget Tuning (Exploratory)

- **FR-030**: The Activities and Repo Status widgets MUST be resized to use less horizontal space than the current implementation
- **FR-031**: Text in the Activities and Repo Status widgets MUST be increased in size for readability
- **FR-032**: The Repo Status widget MUST display the "host/" portion of "host/repo" entries in a dimmer color so the repository name stands out
- **FR-033**: Right-side data in both widgets MUST be visually aligned with its corresponding row

#### Quasi-Realtime Graphs (Exploratory)

- **FR-040**: The dashboard MUST provide a mechanism (to be determined) to display time-series graphs of GPU usage and VRAM consumption
- **FR-041**: When graphs are active and screen space is constrained, the two lowest-priority client widgets MUST be suppressed
- **FR-042**: The graph display mechanism MUST NOT require a dashboard restart to activate or deactivate

### Key Entities

- **Client Widget**: Compound graphical element representing one client machine; contains arc gauges, health indicator, text boxes, hostname bar, and disk bars
- **Development Command**: A Redis-published instruction that triggers a transient developer overlay or reference widget on the dashboard
- **Arc Gauge**: A half-ring (lower hemisphere) visualization showing a percentage value as a filled arc against a background track
- **Disk Bar**: A horizontal bar showing disk usage with color-coded fill, used/total amounts, and drive label
- **Time-Series Graph**: A rolling chart showing metric values over time (GPU usage, VRAM), updated with each telemetry poll

## Success Criteria *(mandatory)*

### Measurable Outcomes

- **SC-001**: 5–6 client widgets render simultaneously on a 1920×1080 display without overlap or truncation
- **SC-002**: A developer can toggle grid overlay and text size reference via Redis commands within 2 seconds of publishing the command
- **SC-003**: Client widget gauge values match reported telemetry data within 1% of the displayed percentage
- **SC-004**: Disk bar colors correctly reflect usage tiers across all reported disks
- **SC-005**: The Repo Status widget "host/" prefix is visually distinct (dimmer) from the repository name when viewed from 1.5 meters
- **SC-006**: Text in Activities and Repo Status widgets is legible from normal viewing distance (1–2 meters)
- **SC-007**: Client widgets for machines without a GPU render cleanly with gray arcs and no GPU text artifacts
- **SC-008**: GPU usage and VRAM graphs (when implemented) update at least once per telemetry poll interval (~5 seconds)

## Assumptions

- The existing MVP dashboard (sprint 001) is complete and merged to main; all existing widgets, Redis polling, and LVGL rendering are functional
- The display resolution is 1920×1080 (the Pi 5 is connected via HDMI at this resolution for this sprint, not the 4K mode documented in architecture)
- The Python client (`kpidash-client`) will be updated to report the OS name field using `platform.system() + " " + platform.release()`
- Arc gauge implementation will use stacked `lv_arc` objects as a compound widget (not a custom draw function) — this is an implementation preference, not a hard constraint
- Widget tuning outcomes (exact sizes, font sizes, spacing) will be determined iteratively and documented after exploration
- The quasi-realtime graph approach (reserved space vs. page switching) will be determined during exploration
- Development commands use a Redis key pattern consistent with the existing `kpidash:` namespace convention
- Priority client ordering for suppression (FR-041) reuses the existing `KPIDASH_PRIORITY_CLIENTS` configuration
- The sketch in `.scratch/client-mockup.png` is a rough starting point; final proportions will be refined during iteration
