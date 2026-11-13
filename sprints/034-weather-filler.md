# 034 — Weather filler in the host row: C · Range, 0–4 columns

Proposal korg:4577 covers WI 4576. It ran as a karc leg (kpidash-c93e24) on
kai under an overseer.

## Goal

Draw weather in the columns the hosts leave free in row 0, to the "C · Range"
mock-up Ken picked on 2026-11-12: current conditions on the left, a
low-to-high bar per forecast day on one shared temperature scale, and the
local day as a bar along the bottom. It reads `kdash:weather:home` (kdashdata
contract kdash-weather v1), written by the new kweather collector, and takes
0 to 4 host-width columns. Nothing depends on it.

Design: `planning/weather-mockups.html` (function `vC`, with `icon`, `pal`,
`dayFrac`), drawn beside `planning/row1.png`, the live row it was drawn
against. The picked design at 4/3/2/1 columns in four states was
`wx-C-states.png` (staged at kai:/scratch/kpidash-weather-mockup/).

## Premise check

| claim | verdict |
|---|---|
| The contract is in kdashdata | **holds**, but this repo's `lib/kdashdata` was pinned at `4837473` (sprint 020), which predates it. Bumped to `01253fd` (sprint 021): contract docs and the schema only, no C change to `kdash_core`. |
| kdash:weather:home is publishing | **holds.** Read once on 2026-11-12 at 16:45 PST (GET only): an NWS record with seven days, `ts` 16:41. It is the fixture. |
| The central Redis is the one kpidash reads | **holds.** kdashdata's `KDASH_CENTRAL_REDIS` is `rpi53:6379`, the panel's own Redis. |
| rpi53's local time is Ken's | **holds.** `timedatectl` on rpi53: America/Los_Angeles, so `localtime_r` gives the "dashboard's own timezone" the brief asks for with no configuration. |

## Decisions

- **Columns.** `wx_columns(host_cols)` = min(4, 10 − host columns), where host
  columns come from the fill rule (`hostcols_used`, new in `hostcols.c`): the
  host count up to ten, and the whole row past ten, when compact tiles take
  the rest. So more than ten hosts is n = 0, never negative.
- **Position.** Right after the last host column, plus ½ · 375 · (6 − hosts)
  with six or fewer, so the 4-column card is centred in what is left
  (`wx_left`, pinned in the test for 0 hosts). `HOSTCOLS_X0/Y0` moved into
  `hostcols.h` so the filler lines up with the columns from one definition.
- **A strict reader, because the counterexamples are the rules.** `wx_parse`
  distrusts a record whose `cond` is outside the closed set, whose `ts` or sun
  events exceed 1e11, whose percents are outside 0..100, whose temperatures
  are outside ±150, which has more than 8 days, or which lacks a required
  field (absent is not null). A distrusted record is logged once, and the last
  good one stays. `source` is diagnostic and is not policed for length.
- **Freshness** is the reader's: stale after 1 h by `ts`. An absent key keeps
  the last record, ageing. No record seen this run: not drawn.
- **Age text** has minute resolution: "just now", "4 m ago", "2 h ago",
  "STALE · 2 h". So it changes at most once a minute.
- **The "now" marker is wall-clock now**, not the record's `ts` (which the
  mock-up used): the brief says "a marker for now". At minute resolution.
- **Icons are drawn, not pre-rendered.** Circles (rounded rects), round-capped
  lines and four triangles for the bolt, all opaque. The moon is a disc with a
  disc of the card's own fill cut from it. Images would need alpha (every
  pixel of the image blended against the framebuffer on the Pi); drawn
  primitives blend only their anti-aliased edges, recolour for the stale state
  for free, and need no asset pipeline. They are repainted only when a record
  arrives, so their cost is per ten minutes, not per second.
- **Opaque only.** The range bar is an LVGL vertical gradient with two opaque
  stops, peach to blue. Twilight is the mock-up's pre-mixed `#857C6C`. The card
  fill is the host columns' `lv_color_mix(Surface0, Base, 66)`.
- **Repaint scope.** One callback-drawn object, as the host columns. A new
  record, a width change or the stale flip repaints the whole card. Otherwise
  only the age line (a 56 px strip) or a 38 px strip around the day bar is
  invalidated when its minute moves.
- **Font.** `lv_font_montserrat_bold_96_digits`: 0-9, minus, degree and em
  dash, 128 KB of source. LVGL's built-in regular Montserrat has no "·" or
  "—", so the regular 20 px lines draw those two in bold 20 (the 10G card's
  technique, done as runs inside the draw callback).
- **Forecast pitch** is by width, not by day count (the mock-up's
  `rw / [0,0,3,6,8][n]`), so a seven-day record at four columns leaves the
  eighth slot empty rather than spreading the bars.

## What shipped

- `src/wxstate.{h,c}` (new): parse and trust, freshness, space, the
  temperature scale, day fractions and every string.
- `src/widgets/weather_card.{h,c}` (new): the card and its ten icons.
- `src/redis.c`: `redis_poll_weather`, one GET per poll; `KDASH_KEY_WEATHER` in
  `protocol.h`. Read only.
- `src/ui.c`: the card is created over row 0 and placed from the host count
  each second; position and visibility change only on a transition.
- `src/hostcols.{h,c}`: `hostcols_used`, `HOSTCOLS_X0/Y0`.
- `fonts/lv_font_montserrat_bold_96_digits.c`, `fonts/generate.sh`,
  `lv_font_custom.h`, `CMakeLists.txt`.
- `tests/test_wxstate.c` (105 checks): every schema example parses, all five
  counterexamples are distrusted, the live record's times, weekdays, scale and
  day fraction, the space rule at every host count that matters, null
  handling, ageing, and the observe/keep-last cycle. `test_hostcols` gains
  `hostcols_used`. `tests/shell/test_font_coverage.sh` checks the digits font
  for exactly what it must carry.
- `tests/render_weather.c` (tool): `render_weather out.bmp
  [ok|nws|polar|stale] [1-4]`, the mock-up's four states from the live
  record and the schema's examples.
- `tests/fixtures/weather-home-2026-11-12.json`: the first live record.
- `lib/kdashdata` → `01253fd` (the weather schema).
- Docs: CLIENT-PROTOCOL §8g, ARCHITECTURE (data flow 13, source tree, layout,
  fonts, a Weather Filler section), roadmap.
- `sprints/planning/weather-mockups.html`, `row1.png`.

Gate: `just check` green (16 ctest targets, 180 client tests, font coverage 23
checks). `just check-warnings-full` is clean at `-O2 -Werror`, and its ctest
(17 targets, with the LVGL leak test) passes. The four states at three
columns are attached to the proposal.

## Repaired in passing

- `CLAUDE.md` and the `justfile` listed what `check-release` compiles as seven
  files including `memstat.c`, which only the LVGL leak test links, and
  missing `admitted.c`, `logfilter.c`, `netstate.c` and `nasstate.c`. Both
  lists now match `tests/CMakeLists.txt` (with `wxstate.c`).
- `sprints/planning/roadmap.md` "Next" still carried "what fills the free host
  columns", which this sprint answers. Removed, and 034 put at the top of Now.

## Left for the ship turn

The live krpidss capture of row 0 and the panel CPU check (sprint 033
measured 5.7 % of one core with the rings full) can only be taken once this is
deployed to rpi53. With seven hosts reporting, the card should be at three
columns.

## Deployed

Deployed 2026-11-12 at about 16:58 PST from kai. This was the ship turn, after
the overseer's clearance (korg:4577 comment 4621). The release is squash merge
`3a9bf39` (PR #37).

- **rpi53:** `scripts/deploy.sh` ran from merged `main`. The service is
  active with `NRestarts=0` and reports `3a9bf39 (2026-11-13)`. The aarch64
  build and link printed no warnings (`.scratch/deploy-034-ship.log`,
  grepped). The journal shows no warnings since the restart.
- **Live capture** (`.scratch/live-034-ship.png`, row 0 cropped to
  `live-034-row1.png`): the filler is at three columns, right of the seven
  host columns and aligned to their pitch. It draws a newer kweather record
  than the fixture: cloudy 56°, six forecast days with drizzle, rain and
  clear icons, the range bars on one scale, and the day bar with the marker
  just after sunset. It matches vC and the offline renders.
- **CPU, % of one core, measured on rpi53 from `/proc/<pid>/stat` ticks over
  60 s:**
  - Before the deploy, on the 033 build (up 77 min): 361 ticks, **6.0 %**.
  - After, with the rings full (up 11 min): 361 and 382 ticks, **6.0 %** and
    **6.4 %**.

  The bar was 6.7 % (033's 5.7 % plus a point). It held, and the change is
  within the spread of the same build measured twice.
- **Memory:** the panel's own memory ring (`kpidash:system:mem:ring`) shows
  RSS at 70.9 MB until about 12 minutes of uptime, then 104 MB. The 033
  build had the same 104 MB at 77 minutes, so the step happens in both builds
  and is not this sprint's.
- `kdash:weather:home` was only read, never written.
