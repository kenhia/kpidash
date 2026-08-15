# Research: Exploration Sprint

**Date**: 2026-04-03
**Plan**: [plan.md](plan.md) | **Spec**: [spec.md](spec.md)

## R1: LVGL Arc Gauge for Client Widget

**Decision**: Use stacked `lv_arc` objects as display-only gauges (remove `LV_OBJ_FLAG_CLICKABLE`).

**Rationale**: LVGL 9.2.2's `lv_arc` widget provides all needed capabilities natively:
- Independent styling of background (`LV_PART_MAIN`) and indicator (`LV_PART_INDICATOR`) arcs
- `LV_STYLE_ARC_WIDTH` controls line thickness per-part
- `LV_STYLE_ARC_COLOR` and `LV_STYLE_ARC_OPA` for independent colors per-part
- `lv_arc_set_bg_angles()` defines the arc sweep range; `lv_arc_set_value()` + `lv_arc_set_range(0, 100)` fills proportionally
- `lv_arc_set_rotation()` rotates the reference frame

**Angle coordinate system** (critical for implementation):
- 0° = 3 o'clock (right), 90° = 6 o'clock (bottom), 180° = 9 o'clock (left), 270° = 12 o'clock (top)
- Clockwise rotation
- For lower-hemisphere arcs: background angles 0°–180° (right → bottom → left)
- With `lv_arc_set_rotation(180)`, the 0°–180° background range maps to the lower half

**Half-ring layout with gap**:
- Left arcs (GPU): bg_angles 90°–180° (center-bottom to left) — or use rotation to simplify
- Right arcs (CPU/RAM): bg_angles 0°–90° (right to center-bottom)
- The gap between left and right naturally exists at the 0° and 180° boundaries
- Alternatively, trim 5° from each end for a visible gap: left 95°–175°, right 5°–85°

**CPU split implementation**:
- Two `lv_arc` objects at the same radius within the inner-right slot
- Purple (top-core) arc: drawn first (lower z-order), range 0–100
- Blue (CPU avg) arc: drawn second (higher z-order, overlays purple), range 0–100
- When CPU avg < top-core, the purple arc is visible beyond the blue arc's endpoint
- Both share the same bg_angles; only indicator fill differs

**Alternatives considered**:
- Custom canvas draw: More flexible but harder to iterate; rejected per Simplicity principle
- Single arc with custom events: Would need draw event hooks; unnecessarily complex for display-only gauges

## R2: Arc Sizing and Layout Math

**Decision**: Arc gauges sized relative to widget width; text boxes flank the gauge area.

**Layout geometry** (for ~300px wide widget at 1920×1080 with 6 across):
- Available width: ~1920 / 6 ≈ 320px per widget, minus padding → ~300px usable
- Arc gauge area: centered, ~140px diameter (radius = 70px)
- Outer ring thickness: 10% of 70px = 7px
- Inner ring thickness: 10% of 70px = 7px
- Gap between rings: 3% of 70px ≈ 2px
- Center health circle: ~20px diameter
- Text boxes: ~75px wide on each side of the arc area
- Hostname bar: full widget width, ~30px tall
- Disk bars: full widget width, ~16px each × max 6 = 96px
- Total estimated height: 140 (arcs) + 30 (hostname) + 16 (OS text) + 96 (disks) ≈ 280px

**Note**: These are first-iteration estimates. The dev grid overlay (P2) will be used to refine proportions.

## R3: Redis Command Pattern for Development Commands

**Decision**: Use Redis STRING keys with short TTL, polled in the existing `redis_poll()` cycle.

**Rationale**: The existing pattern (fortune pushed key, status ack key) uses `GET` + `EXISTS` checks in the
poll loop. Development commands fit the same pattern — no need for pub/sub or Lua scripts.

**Key pattern**:
- `kpidash:cmd:grid` — JSON string: `{"enabled": true, "size": 50}` with 300s TTL
- `kpidash:cmd:textsize` — JSON string: `{"enabled": true}` with 300s TTL

**Workflow**: Client publishes with `SET kpidash:cmd:grid '{"enabled":true,"size":50}' EX 300`, dashboard
reads on next poll cycle (~1s), creates/destroys overlay widget. Grid auto-disables after TTL expiry.

**Alternatives considered**:
- Redis pub/sub: Would require a subscriber thread or async callback; adds complexity for a dev-only feature
- Redis RPUSH/LPOP command queue: More infrastructure than needed; TTL-based keys are simpler and self-cleaning

## R4: LVGL Chart Widget for Quasi-Realtime Graphs

**Decision**: Use `lv_chart` with `LV_CHART_TYPE_LINE` and `LV_CHART_UPDATE_MODE_SHIFT` for rolling GPU/VRAM graphs.

**Rationale**: LVGL 9.2.2 includes a full chart widget with native streaming support:
- `lv_chart_set_next_value(chart, series, value)` appends a point and shifts old data left
- `LV_CHART_UPDATE_MODE_SHIFT` provides exactly the rolling-window behavior needed
- `lv_chart_set_point_count()` controls the visible history depth
- `lv_chart_set_range()` sets Y-axis scaling (0–100 for GPU %, 0–max_vram for VRAM)
- `lv_chart_set_div_line_count()` adds grid lines

**Layout integration** (exploratory):
- Option A: Reserve a fixed area below the client widget row, always visible
- Option B: Toggle visibility via a dev command (like grid overlay)
- Option C: Replace the bottom two widgets (activities/repos) when graphs are active
- Decision deferred to iteration per spec

**Data flow**: On each `redis_poll()` cycle, if the graph widget exists, call `lv_chart_set_next_value()`
with the latest GPU compute % and VRAM used MB from the target client. At ~1 poll/sec with 300 data points,
this gives ~5 minutes of history.

**Alternatives considered**:
- Custom canvas line drawing: More control but requires manual buffering; `lv_chart` handles this natively
- External rendering (ffmpeg/framebuffer): Overkill for embedded use

## R5: Client OS Name Field

**Decision**: Add `os_name` to the health JSON payload; client computes via `platform.system() + " " + platform.release()`.

**Rationale**: The OS name is relatively static (changes only on OS upgrade), so including it in the
health JSON (sent every ~3s) is acceptable. It doesn't belong in telemetry (which is performance data).

**Health JSON addition**:
```json
{
  "hostname": "kubs0",
  "last_seen_ts": 1743292800.123,
  "uptime_seconds": 86400.5,
  "os_name": "Linux 5.15.0-173-generic"
}
```

**C-side**: Add `char os_name[128]` to `client_info_t`; parse in `redis_parse_health_json()`.

**Alternatives considered**:
- Separate Redis key per client for OS info: Adds a key per client, more poll round-trips; unnecessary
- Include in telemetry JSON: Could work but health JSON is the identity/status payload — better fit

## R6: Widget Tuning Approach

**Decision**: Iterative sizing guided by dev grid overlay. No fixed targets — use visual feedback.

**Rationale**: The exact font sizes, padding, and row heights for Activities and Repo Status widgets
depend on how they look at 1920×1080 from 1–2m viewing distance. Rather than specifying pixel values
upfront, use the grid overlay and text size reference to iterate.

**Repo host dimming**: Use LVGL's `lv_label_set_text_selection_color()` or, more simply, create two
`lv_label` objects per row — one for "host/" with `lv_obj_set_style_text_opa(label, 128, 0)` (50% opacity)
and one for "repo" at full opacity. Alternatively, use LVGL's recolor feature:
`lv_label_set_recolor(label, true)` and format as `#888888 host/#repo`.

**Alternatives considered**:
- LVGL span widget (`lv_span`): More flexible rich text, but heavier; recolor is sufficient for host dimming
