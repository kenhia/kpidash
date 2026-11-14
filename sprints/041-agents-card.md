# 041 — Agents card in row 3

Proposal korg:4698 covers WI 4695. This is slice 3 of program korg:4699
("kpidash Agents card: 24-hour session timeline on row 3"). It ran as a karc
leg (kpidash-22d9c5) on kai, with an overseer on cleo. The feed contract is
kdashdata sprint 026 (proposal korg:4696, handoffs korg:4700 and korg:4701).
ksitrep's publisher (korg:4697) is being built alongside this slice.

## Goal

On 2026-11-14 Ken picked the "Border colour" view with links "On" from the
agents mock-ups, and added two tweaks recorded in WI 4695. The task was to
draw it as a 4×1 in row 3, columns 0-3, left of the calendar (`ROW_Y(2)`,
`COL_X(0)` in code, 2528 × 628 frame). The mock-up source is committed as
`planning/agents-mockups.html`, and its `borders(true)` function is the
geometry reference. `planning/row3-mockups.html`, which holds the parked
Airspace card (WI 4692), is committed beside it, as the WI asked.

## Premise check

- **The contract exists.** kdashdata `41226c2` (its main, after the
  sprint 026 deploy record, on top of `d6fb322`) has `kdash_parse_agents`,
  `kdash_agents_bar_end`, `KDASH_KEY_AGENTS` and the schema. The submodule
  moved from `0e921a1` to `41226c2`.
- **The calendar record grew, and is already static.** kdashdata 026 grew
  `kdash_calendar_t` to 19,960 bytes. `calstate.c` already held it, and its
  parse scratch, as file statics, and nothing in the card, the render tool or
  the tests copies one onto the stack (grep). So no code change was needed,
  only the two size comments (see "Repaired in passing").
  `test_calstate`, `render_calendar_long_names` and the calendar renders
  pass on the bump.
- **The live key is absent.** Nothing writes `kdash:agents` until korg:4697
  ships, so the card is built and proved against fixtures. Nothing was
  written to central.

## Decisions

- **The model is `agstate.c`, and it is pure.** libkdash parses the record
  and says where each bar's right edge is (`kdash_agents_bar_end`, never
  re-derived). agstate adds the time axis, the packing, the row height, the
  hour grid and the label fit, and takes the clock as an argument. It follows
  the `calstate.c` pattern. One `kdash_agents_t` (21,544 bytes) and one parse
  scratch are statics.
- **The view moves once a minute.** `now` is floored to the minute, so the
  view is a pure function of (record, minute). The widget rebuilds it only
  when one of those two changes, compares it, and repaints the card only if
  it differs. The other 59 polls of a minute cost one integer compare.
- **The card's window is 24 hours, whatever the record's `window_s`.** A bar
  that started before the window is clipped at the left edge.
- **Packing, as the mock-up's `fit`.** Roots go in start order, each in the
  highest row free at its left edge. Each overseer's legs follow at once,
  each in the highest free row *below* it, and placement recurses for nested
  legs. A row is free 16 px after its last bar's end, which leaves room for
  a ship's diamond. The record's order does not matter: a leg listed before
  its overseer is still placed under it (tested). If two overseers name each
  other, so there is no root, the leftovers are placed as roots, and nothing
  is silently undrawn.
- **The rows past the 20 px label (the WI's open question).** A row is
  76 px at most and shrinks as rows are added. The floor is **36 px**: the
  bold 20 face's 25 px line, plus the 4 px border and a 3 px margin each
  side. That gives **10 rows** ((452 + 10) / (36 + 10)). Past that, rows do
  not shrink further. Instead, the bar that ended longest ago is left out,
  and the rows are packed again until they fit. An overseer that is left out
  takes its legs with it. The header says "N older not shown" in muted text,
  between the host legend and the state legend. The reasoning: the newest
  work is what the card is for, and a label under 20 px is against the
  panel's rule.
- **Square ends.** A running bar (working or waiting, and still reaching
  now) ends square at the now line. It is drawn as the rounded bar, then a
  14 px patch over its right end, bordered on top, bottom and right
  (`border_side`). That needs no one-sided radius, so the WI's fallback was
  not needed.
- **A working overseer keeps the dark fill**, so its peach name stays
  readable, as the WI said. **A waiting overseer is yellow with a dark
  name**, because peach does not read on yellow. The WI did not cover that
  case; it is flagged below.
- **Names.** A name is drawn only when it fits: 14 px in from the left, and
  8 px short of the right edge, or 18 px with a ship, whose crust-outlined
  diamond reaches 16 px into the bar. The mock-up's own test allowed 0 px on
  the right, which is why its "kdashdata" ran under the diamond. A name that
  does not fit is cut with "…" if at least 4 characters survive. Otherwise
  the bar has no name. So the mock day draws "kdashd…" and "kpida…" where
  the target let the text run under the diamond.
- **Glyphs the face lacks.** Names are any UTF-8 (CD-31). A code point the
  bold 20 face lacks becomes "?" once per change, before the name is measured
  and drawn. Layout stays exact, and no box or per-redraw LVGL warning appears
  (WI #2646). The long render shows "?????? ???" for a Cyrillic name.
- **Ship diamond.** It is one butt-ended line along the diagonal, as wide as
  it is long: a single draw task with no triangle seam. Crust 24 px, then
  teal 20 px.
- **Parser drops are a log line.** If an adopted record has
  `bars_skipped`, `bars_truncated` or `parents_dropped`, redis.c logs one line
  on the change. The panel shows nothing about it.
- **States.** No feed: the grid, plus "no agents feed" in the plot. Empty:
  "no sessions in the last 24 hours". Both messages sit on a backdrop patch
  so that no grid line runs through them. Stale (past
  `expected_refresh_s` + 60 s) shows a muted "as of 25m" in the header, and
  running bars stop at their own `end`. An absent key or a rejected record
  keeps the last good record.

## What shipped

- `src/agstate.{h,c}`: the model.
- `src/widgets/agents_card.{h,c}`: the card at 2520 × 620, in the mock-up's
  coordinates.
- `src/redis.c`: `redis_poll_agents()`, one read-only GET per poll through
  `poll_status_key()`. `src/ui.c`: the slot at `COL_X(0)`, `ROW_Y(2)`,
  always shown.
- `tests/test_agstate.c` (515 checks). It covers:
  - the schema's 3 examples and 14 counterexamples;
  - the mock-up's day, row by row as the target draws it;
  - the minute step;
  - window clipping, with a bar 30 h long and one ending an hour in;
  - minimum width;
  - no feed, empty, and hold through an absent key or a rejected record;
  - the cadence edge (119 s fresh, 180 s stale);
  - crowding from 1 to 40 bars;
  - an overseer leaving out its legs;
  - a working overseer with running legs listed out of order;
  - the leg floor;
  - the label fit, including UTF-8 and the 31-character bound;
  - the hour grid across the fall-back DST day.

  Six defects were planted one at a time, and every one now fails the test:
  - legs not placed below their overseer;
  - the newest bar dropped instead of the oldest;
  - running bars ignoring staleness;
  - no minute floor;
  - an overseer dropped without its legs;
  - no packing gap.

  The first and the fifth passed the tests as first written. The
  leg-floor test and the legs-end-later fixture were added for them.
- `tests/render_agents.c` (tool): `render_agents out.bmp
  mock|empty|nofeed|stale|crowded|legs|long`, or `live agents.json [time]`.
  Every state fails if anything other than a ship's diamond is drawn right
  of the now line. `crowded` and `long` run as ctest tests in the full tree.
- `planning/agents-mockups.html` and `planning/row3-mockups.html`.
- Docs:
  - ARCHITECTURE.md: data flow 17, the source tree, an "Agents Card"
    section, and the layout diagram;
  - CLIENT-PROTOCOL.md §8k;
  - the `check-release` file list in CLAUDE.md,
    `.github/copilot-instructions.md` and the justfile;
  - the roadmap;
  - README's layout.

## Renders

All are 2528 × 628, the card's whole frame, under
`kai:/scratch/kpidash-agents/renders/` and attached to korg:4698:

| render | image |
|---|---|
| mock | img-1264 |
| empty | img-1265 |
| nofeed | img-1266 |
| stale | img-1267 |
| crowded | img-1268 |
| legs | img-1269 |
| long | img-126a |
| live (schema example 1) | img-126b |

- **mock** was compared with `agents-card-target.png`. The header, legend
  positions, plot, grid, every bar's row and x, the stems, the diamonds and
  the now line all match. The only differences are the two cut names
  described above.
- **crowded**: 30 overlapping bars, 10 rows at 36 px, "20 older not shown".
- **legs**: a working cleo overseer (dark fill, peach name, square end)
  with a finished shipped leg and two running legs below it.
- **live**: the contract's own first example, read through `ag_observe`.

## Gate

- `just check` passes:
  - ctest 20/20, including `test_agstate`;
  - `check-release` builds `agstate.c` at `-O3 -Werror`;
  - client 159, units, scripts 12, fonts 24.
- `just check-warnings-full` builds the whole tree at `-O3 -Werror`,
  including `agents_card.c`, `ui.c`, `redis.c` and `render_agents`, with
  zero warnings. Its ctest passes 24/24.

## Pre-ship reading

Panel CPU before the ship was **8.4 % of a core**. This is
`kpidash.service` pid 327432, 1009 ticks of `utime + stime` at 100 Hz over
120 s, ending 12:13 PST on 2026-11-14. It was read from `/proc` on rpi53
over ssh from kai, and no second process was started. The acceptance
compares the post-deploy reading, measured the same way, against this
figure. Expect one full-card repaint (2520 × 620, opaque) per minute.

## Repaired in passing

- **The calendar's size notes were stale after the pin bump.** `calstate.h`,
  `calstate.c` and ARCHITECTURE.md said `kdash_calendar_t` was "about
  5.8 KB"; it is now 19,960 bytes. Gate: prose, checked by reading; the
  struct was already static.
- **README's and ARCHITECTURE.md's layout diagrams** still showed row 1
  "then free" and row 2 "free", which was stale since sprints 037 to 040.
  They now show every card. Gate: prose.

## Left for the ship turn

- Deploy with `scripts/deploy.sh`, and read the aarch64 link output by eye.
- A `krpidss` capture of row 3. Until ksitrep's publisher (korg:4697) is
  live, the card shows the grid and "no agents feed", which the overseer has
  already accepted for a few hours.
- Panel CPU after the deploy, against 8.4 %.
