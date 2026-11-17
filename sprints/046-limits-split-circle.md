# 046 — Claude limits card: split-circle gauges

Proposal korg:4802 covers WI 4801. It ran as a karc leg (kpidash-a97e8b) on
kai under an overseer. Branch `046-limits-split-circle`.

## Goal

Ken, 2026-11-16: on the Claude limits card the white pace tick ("how far
through the window") does not read, because a full ring has no visible start
or end for the tick to be read against. The fix is to open each ring into a
split circle like kctrldeck's Agents gauges: a 270° arc starting at 135° in
LVGL's angles (0° at three o'clock, clockwise), so the gap is centred at the
bottom.

## Premise check

- **The premise holds.** `draw_ring` drew the track as `arc(0, 360)` and the
  value from 270° (twelve o'clock) clockwise, and placed the tick on the
  full circle.
- kpidash is not in the cross-project-planning routing table.
- The preflight was clean: an empty tree, and local `main` level with
  `origin/main`.

## Decisions

- **The geometry lives in limstate, not the widget**: `LIM_GAUGE_START_DEG`
  (135), `LIM_GAUGE_SWEEP_DEG` (270) and `lim_gauge_deg(frac)`, which clamps
  to the arc. The widget only compiles in the full tree, so this puts the
  angle arithmetic under the `just check` gate (`test_gauge_deg` in
  `test_limstate.c`). The clamp means a tick can never sit in the gap.
- **The track has rounded ends now too**, as the WI asks. Before, it was a
  closed circle and had no ends to round.
- **pct 100 needs no special case any more.** The old code swept
  `0.99999 × 360` so that LVGL would not treat a full circle as empty. A
  270° value arc from 135° to 405° is not degenerate: LVGL's software arc
  normalises 405° to 45°.
- **`PACE_STEPS` stays 720.** It now gives a resolution of 0.375° on the
  270° arc, finer than before. Only its comment changed.
- **The number, label and reset line stay where they were.** With the gap at
  the bottom the number still sits inside the arc, and the label and reset
  line already sat below the ring. No move was obviously better.
- The bar, its tick and the caption are unchanged.

## What shipped

- `src/limstate.{h,c}`: the gauge constants and `lim_gauge_deg`.
- `src/widgets/limits_card.c` `draw_ring`: the track runs from 135° to 405°
  and the value from 135° to `lim_gauge_deg(pct/100)`, both rounded. The
  tick is at `lim_gauge_deg(pace)`, using LVGL's cos/sin convention in place
  of the old from-twelve sin/−cos.
- `tests/test_limstate.c`: `test_gauge_deg` (the ends, twelve o'clock, a
  quarter, and the clamp at both ends), written first and seen failing.
- `tests/render_limits.c`: a parametric `gauge` state,
  `gauge <five|scoped|seven> <pct1> <pace1> <pct2> <pace2> [stale]`, for
  reviewing the rings at any value and pace in each layout.
- `sprints/planning/limits-split-circle-046.png`: the review sheet. It shows
  three layouts × five cases: 0 %, low, mid-to-high and 100 %, with the tick
  short of and past the value end, and a stale card. It was built by
  `/scratch/kpidash-limits/sheet.py` from `build-warnings-full`'s
  `render_limits`. The per-render PNGs are beside the script.

## Noticed, not changed

- **"100%" at the 96 px size is wider than the inside of the ring**, so its
  "1" and "%" overlap the stroke at about nine and three o'clock. This is
  older than this sprint: the number's position, the ring radius and the
  stroke are all unchanged, and the old full ring was drawn at those angles
  too. Fixing it is a type-size or layout choice, so it is flagged for the
  overseer, not repaired here.

## Gates

- `just check`: green, 23/23 ctest, the shell tests and the client.
- `just check-warnings-full`: green, the whole tree at `-O3 -Werror`.
