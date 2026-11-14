# 042 — Agents card v2: last 3 hours plus a 24-hour strip

Proposal korg:4734 covers WI 4733. This is slice 7 of program korg:4699. It
ran as a karc leg (kpidash-d847bc) on kai, with the overseer on cleo. Branch
`042-agents-card-v2`.

## Goal

Sprint 041's card put the whole day on one axis. The first live day showed
real sessions running 10 to 30 minutes, about 25 px on that axis, so no name
ever fit. Ken compared mock-ups and ruled "B, last 3 hours". The card now has
two parts:

- a **detail area** for the last 3 hours, holding everything 041 drew;
- a **day strip** under it for all 24 hours: thin lanes, one per host, no
  names.

The feed (`kdash:agents` v1) is unchanged. The geometry reference is
`focus(3)` in `planning/agents-mockups-v2.html`, committed here. The target
render is `kai:/scratch/kpidash-agents/agents-card-target-v2.png`.

## Premise check

- **The premise holds.** 041's card was live on rpi53 (`b00da0b`), with the
  24-hour axis in `agstate.c` (`AG_WINDOW_S`) and names only inside bars.
- **The live feed has real data.** At 13:51 PST rpi53's `kdash:agents` held
  7 bars, the first starting at 12:24, including this leg's own `kpidash` bar
  parented to the cleo overseer.

## Decisions

- **The model decides where each name goes, so `ag_view` takes the font's
  measure.** Packing needs a name's extent when the name sits outside its
  bar, and only the font knows the width. The widget passes
  `agents_card_measure`, which swaps in the '?' glyphs and then measures, so
  the width it reports is the width that is drawn. The tests pass a 12 px
  per-character measure. With no measure, the view draws no names.
- **The name rule: never shortened while there is room to write it whole.**
  The first round built the WI's literal order, which tries the "…" cut
  inside before going outside. The live render then drew two short legs as
  "ksit…" and "k-home…", with room on their left for both names whole. I
  flagged it, and the overseer ruled for this order, which is what Ken was
  shown:
  1. the whole name inside;
  2. the whole name left of the bar (right-aligned 8 px, host colour, peach
     for an overseer), when it stays right of the plot's edge **and the row
     the bar takes has room**;
  3. a name over 20 characters cut to 20 with "…", outside on the same
     terms, unless the inside cut keeps 20 or more;
  4. the "…" cut inside, at four or more characters (041's rule);
  5. no name.

  Two consequences of how it is built:
  - **The name is chosen during packing, not before it.** Step 2's "the row
    has room" depends on which row the bar lands in. `name_forms()` measures
    each bar's forms once per view; `place()` picks among them against the
    row it is about to use.
  - **A name never moves its bar.** The first round let an outside name push
    its bar down a row. Under the ruling, a name with no room in the bar's
    own row falls to step 3, 4 or 5 instead. So the extent check now guards
    the room on the left, and nothing overlaps.
- **Packing keeps 041's 16 px row gap.** The mock-up uses 12 px, plus 12 px
  after a shipped bar. So on the target day `kpidash` shares row 1 with
  `kdashdata` (as close as 16 px after its end, past the diamond), where the
  target PNG put it on row 3. Row count, x positions and every other row
  match.
- **The detail window is one constant**, `AG_DETAIL_S` (10800). The header's
  "last 3 hours", the quiet message and the axis all follow it.
- **Rows.** 104 to 430, 8 px gaps, 64 px at most. The 36 px floor is
  unchanged, so `AG_ROWS_MAX` drops from 10 to 7. Past 7 rows the oldest bars
  are left out, as before.
- **Grids.** The detail grid has a line every 30 minutes. The model emits
  each local half hour, so a zone with a :30 offset still lands on the clock.
  The strip keeps 041's 2-hourly `hours()`.
- **An empty detail area with a non-empty strip** is a new status,
  `AG_QUIET`. It shows a muted "nothing in the last 3 hours" on a backdrop
  patch, as the WI suggested. The strip's band and grid draw in every state,
  including no feed.
- **Strip draw order.** Waiting (yellow) bars are drawn after the rest, so a
  needs-you stretch is never hidden under a lane-mate. The mock-up gets the
  same effect from its record order.
- **The strip band.** It runs from y 478 to 562: 486 − 8 to 554 + 8, as the
  mock-up draws it. The WI's "478 to 558" is 4 px short of its own
  lane-and-padding numbers. The PNG agrees with 562, and so does this
  sprint.

## What shipped

- `src/agstate.{h,c}`:
  - the detail window, with clipping at its left edge;
  - name placement inside or outside the bar, and packing on the name's
    extent;
  - the half-hour ticks;
  - the strip: lanes, the 4 px minimum, waiting bars, and the band's
    position;
  - `AG_QUIET`.
- `src/widgets/agents_card.{h,c}`:
  - the v2 geometry;
  - right-aligned outside names;
  - `draw_strip()`;
  - the quiet message;
  - `agents_card_measure()`, exported for the render tool.
- `tests/test_agstate.c`, 557 checks:
  - the target day, by x position, window, ticks, band and strip lanes;
  - names, by the ruling's order:
    - whole outside preferred to the "…" cut inside;
    - a name with no room in its row left unnamed rather than moving its bar;
    - the inside cut when the left is blocked;
    - a 31-character name cut to 20 outside when the inside cut keeps only
      5, but cut inside when that keeps 24;
    - the left edge;
    - no measure;
  - the strip on its own: the quiet status, the minimum width, lanes, and a
    sliver at the now edge;
  - clipping on both axes;
  - crowding re-anchored to 7 rows;
  - the leg floor and the working overseer re-anchored into the 3 hours.

  **Defects were planted one at a time, and the tests fail on every one.**
  First round, eight:
  - packing that ignores the outside name;
  - no drop at the left edge;
  - no detail clip;
  - no strip minimum width;
  - lanes collapsed into one;
  - the band in the wrong place;
  - no quiet state;
  - a whole-name-only inside rule.

  After the ruling, seven more:
  - no whole-outside step;
  - no row-room check;
  - no left-edge check;
  - no cut-to-20 outside;
  - cut-to-20 even when the inside cut keeps more;
  - no inside-cut fallback;
  - no packing gap.
- `tests/render_agents.c`:
  - new `target` and `quiet` states;
  - `crowded`, `legs` and `long` moved into the 3-hour window, and `long`
    gained a narrow bar named on its left and one at the edge left unnamed;
  - the stray-pixel check now covers the strip's lanes too.

  `target` and `quiet` are now ctest tests in the full tree, beside
  `crowded` and `long`.
- `planning/agents-mockups-v2.html`.
- Docs:
  - ARCHITECTURE.md: data flow 17, the source tree, the layout diagram, and
    the "Agents Card" section;
  - CLIENT-PROTOCOL.md §8k;
  - README's layout;
  - the roadmap.

## Renders

All renders are 2528 × 628. They are in
`kai:/scratch/kpidash-agents/renders-v2/` and attached to korg:4734.

| render | image |
|---|---|
| target | img-128a (first round img-127f) |
| live (rpi53, 13:59 PST) | img-1288 (first round, 13:51: img-1280) |
| quiet | img-1281 |
| long | img-1289 (first round img-1282) |
| crowded | img-1283 |
| legs | img-1284 |
| nofeed | img-1285 |
| stale | img-1286 |

- **The target render, compared with `agents-card-target-v2.png` pixel by
  pixel** (more than 40 levels apart in any channel):
  - strip **0.08 %**;
  - header 1.5 %, grid labels 0.6 %, strip labels 0.8 %: browser text
    rendering against LVGL's;
  - detail area 5.5 %, which is the `kpidash`/`k-homelab` row swap from the
    packing gap above.

  Bars, x positions, colours, stems, diamonds, the band and the grid all
  line up.
- **live** is rpi53's `kdash:agents`, read at 13:59 PST and drawn at that
  minute. It shows:
  - the finished `kpidash-refresh` overseer with four legs, including this
    leg's `kpidash` (working, solid peach, square end);
  - the 2-minute `k-homelab`, 6-minute `ksitrep` and 9-minute `k-homelab`
    legs all named whole on their left. In the first round, at 13:51, the
    last two were "ksit…" and "k-home…";
  - the cleo overseer "Stale apt-temps on rpi53 dashb…" (the publisher's own
    cut), now two bars, 12:24-12:47 with its `apt-temps` leg and a sliver at
    13:58, each named whole on its left.

  The strip holds only the last 1.5 hours, because the feed's first bar is at
  12:24 today.
- **long** shows step 3: "low-hanging-fruit sweep round 2" (31 characters)
  has no room for its whole name left of its bar, so it reads
  "low-hanging-fruit s…" there.

## Gate

- `just check` passes:
  - ctest 20/20, including `test_agstate` (557);
  - `check-release` builds `agstate.c` at `-O3 -Werror`;
  - client, units, scripts 12, fonts 24.
- `just check-warnings-full` builds the whole tree at `-O3 -Werror` with
  zero warnings. Its ctest passes 26/26.

## Pre-ship reading

Panel CPU before the ship was **9.0 % of a core**: `kpidash.service`
pid 382238, 1075 ticks of `utime + stime` at 100 Hz over 120 s, ending
13:53 PST on 2026-11-14. It was read from `/proc` on rpi53 over ssh from
kai, with no second process started. This is 041's live build with real bars
(041 read 6.5 % after its deploy, when it showed "no agents feed"). The card
still repaints at most once a minute, and it gains one band, a grid and up
to 96 small opaque rectangles.

## Repaired in passing

- **The `long` render's 32-character name.** "low-hanging-fruit sweep,
  round 2" is one character past the contract's 31, so the parser skipped
  that bar and `long` has drawn without it since 041. It is now "low-hanging-
  fruit sweep round 2" (31). Gate: the bar appears in the `long` render
  (img-1289), which the `render_agents_long_names` ctest runs.

## Left for the ship turn

- Deploy with `scripts/deploy.sh`, and read the aarch64 link output by eye.
- A `krpidss` capture of row 3.
- Panel CPU after the deploy, against 9.0 %.
