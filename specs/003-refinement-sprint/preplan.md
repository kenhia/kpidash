# Pre-Plan: Sprint 003 — Refinement & Standardization

**Status**: Draft
**Predecessor**: Sprint 002 (Exploration Sprint)
**Branch**: TBD (e.g., `003-refinement-sprint`)

---

## Sprint Goal

Refine and harden the codebase established in Sprint 002. Formalize the
widget unit system, improve dev tooling (unit-aware grid), and address
known rough edges before building new features.

---

## 1. Widget Unit System Formalization

### Background

During Sprint 002, an ad hoc "unit" system emerged based on the client card
widget dimensions. This needs to be formalized as a first-class concept.

### Unit Definition (at 3840×2160)

| Size | Name | Width | Height | Description |
|------|------|-------|--------|-------------|
| 1×1 | Single | 624 | 620 | Base unit (client card) |
| 2×1 | Double-wide | 1256 | 620 | Activities, Repo Status, Dev Graph |
| 0.5×1 | Half | 308 | 620 | Potential compact widget |
| 1×0.5 | Short | 624 | 306 | Potential summary widget |
| 2×0.5 | Wide-short | 1256 | 306 | Potential header/stats bar |

**Derivation**:
- Base unit width: `(3840 - 16 - 5×8) / 6 = 624px` (6 across, 8px pad, 8px margin each side)
- Base unit height: `620px` (empirically tuned for card content)
- Gap: `8px` between units
- 2×1 = `624 + 8 + 624 = 1256px`

### Tasks

- Define unit constants in `protocol.h` or a new `layout.h`:
  `UNIT_W`, `UNIT_H`, `UNIT_GAP`, plus macros for computing Nx×Ny sizes
- Refactor `ui.c` to use unit macros instead of magic numbers
- Document the unit system in `docs/ARCHITECTURE.md`
- Consider whether unit sizes should be computed from screen resolution
  (supporting 1920×1080 in addition to 3840×2160)

---

## 2. Unit-Aware Dev Grid

### Background (from `.scratch/refine-dev-grid.md`)

The current dev grid draws a pixel-based grid. With the unit system formalized,
it should support unit-based grid lines to visualize widget placement.

### Proposed Commands

```bash
# Draw grid at 1 unit spacing
redis-cli SET kpidash:cmd:grid '{"enabled":true, "unit": true, "size":1}' EX 300

# Draw grid at 2 unit spacing
redis-cli SET kpidash:cmd:grid '{"enabled":true, "unit": true, "size":2}' EX 300

# Draw grid at 0.5 unit spacing
redis-cli SET kpidash:cmd:grid '{"enabled":true, "unit": true, "size":0.5}' EX 300

# Pixel-based grid (existing behavior)
redis-cli SET kpidash:cmd:grid '{"enabled":true, "size":50}' EX 300
```

### Tasks

- Extend `dev_cmd_state_t` with `grid_unit` boolean field
- Parse `"unit"` field from grid command JSON in `redis.c`
- Update `dev_grid.c` to compute line positions from unit system when
  `grid_unit == true` (size × UNIT_W horizontal, size × UNIT_H vertical,
  offset by margins/gap)
- Add unit tests for the new JSON field
- Update `docs/CLIENT-PROTOCOL.md` with the new field

---

## 3. Codebase Quality Improvements

### Known Issues

- **kwi #22**: Repo Status widget flashes "All repos clean" on data refresh
  - Root cause: row rebuild triggers even when data hasn't changed
  - Cache (`repos_equal()`) helps but doesn't fully prevent flash
  - Investigate: compare at poll level before calling widget update

- **Activities widget rebuild**: Same potential flash issue as repos
  - Apply cache pattern from repo_status to activities

### Code Organization

- Extract common widget patterns (color palette, font constants, cache
  comparison helpers) into shared headers
- Consider `widgets/common.h` for `COLOR_BG`, `COLOR_FG`, `COLOR_MUTED`,
  `COLOR_HEADER`, etc. (currently duplicated across all widgets)
- Standardize border/radius/padding styles

### Testing

- Add C tests for any new layout computation functions
- Consider integration smoke test: build + parse known Redis fixture data
  + verify registry state

---

## 4. Client Widget Refinement (Carry-Forward)

Minor adjustments identified during Sprint 002 iteration:

- Client card height may need adjustment when formalizing to exact 1×1 unit
  (currently `card_area_h=640` for the grid area, cards are 620px tall)
- Disk bar text readability at different fill levels — may need shadow/outline
- Client card ordering: currently first-come-first-served; consider priority
  ordering (user mentioned wanting this in Sprint 002 notes)

---

## 5. Documentation

- Update `ARCHITECTURE.md` with formal unit system diagram
- Add a `docs/WIDGET-UNITS.md` reference page
- Review and finalize `CLIENT-PROTOCOL.md` (ensure all Sprint 002 additions
  are accurately documented)

---

## Open Questions

1. Should the unit system be resolution-independent (compute from screen size)
   or hardcoded for 3840×2160?
2. Priority client ordering — config-driven or based on some heuristic?
3. Should widget shared colors/styles be a compile-time header or runtime theme?

---

## Source Material

- `.scratch/refine-dev-grid.md` — Unit-aware grid requirements
- `.scratch/refine-client-widget.md` — Client widget iteration notes
- `.scratch/refine-graph-widget.md` — Graph widget refinement notes
- `.scratch/refine-activities.md` — Activities table layout (implemented)
- `.scratch/refine-repo-status-widget.md` — Repo card design (implemented)
- `kwi #22` — Repo Status flash bug
