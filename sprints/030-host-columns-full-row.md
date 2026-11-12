# Sprint 030: host columns across the full row

Proposal korg:4545 covers WI 4543 (take the full top row, double the text) and
WI 4544 (one row for the fullest drive). Ken could not read sprint 029's 14 px
columns from his chair. He measured a column at 21.5 mm on the panel and
approved a full-row mock-up at true physical size (2026-11-12). The overseer
drew it as `planning/hosts-wide-mockups.html` (md5
`f341d69ee0e8aa823956ae67a3973e9b`), which is this sprint's spec and is
committed with it. The sprint ran as karc leg `kpidash-57b298` on kai,
headless, under `/overseen-sprint`.

## Premise check

| claim | verdict |
|---|---|
| the widget is 3 cells wide at row 0 cols 0-2, and the rest of row 0 is empty | **holds** (`HC_W UNIT_W_N(3)`, `ui.c`) |
| text is 14 px (labels, uptime, OS) | **holds** (`FONT_LABEL` was montserrat 14) |
| the repo has Montserrat regular 20 and bold 20/28/36 | **holds**: regular 20 is an LVGL built-in (`lv_conf.h`), bold 20/28/36 are generated in `fonts/` |
| a full column lists up to five drives | **holds** (`HOSTCOLS_DISKS 5`) |
| the compact tile already shows the fullest drive | **holds** (`worst`) |
| top core is drawn white (029's choice) | **holds** |

## Decisions

### Colours (Ken, 2026-11-12)

Three colour rulings, from the proposal notes and the comment on WI 4543. The
mock-up already shows all three.

- **Top core** (graph line, figure, the compact tile's tick) is `#AB47BC`,
  replacing 029's white. It is named `WS_COLOR_TOP_CORE` in `common.h`, with a
  comment saying it is Ken's pick and not a Mocha accent.
- **RAM figure** takes the RAM bar's mauve, or yellow/red when RAM alerts.
- **VRAM "used / total"** figures take the VRAM bar's peach. This ruling
  exists only in the notes and in comment 4599 on WI 4543.

### Model (WI 4544): `hostcols`

- `hc_host_t` drops the five-row `disks[]` list and `worst`. It now carries
  `has_disk`, one `disk` (the fullest by percent), and `disk_more`, the count
  of the host's other drives at or over the warn line. Nothing draws a drive
  list any more, so keeping the list would only invite drift.
- **Attention is judged on every reported drive**, not the five fullest as
  before. Because the drawn drive is the fullest, it is always the one that set
  the border.
- **Ties:** the first drive reported wins. The other still counts toward `+N`
  when it is over the line.
- New `test_fullest_drive` covers the cases the WI names: one drive, several
  with none over, two over (`+1`), and a tie. It also covers a critical drive
  appearing beside two warnings (`+2`, red), and seven drives, the case the old
  five-row cut would have truncated.

### Widget (WI 4543): `src/widgets/host_columns.c`

Every coordinate is copied from the new mock-up's `colFull`, `tile` and
`widget`, as 029 did:

- 3792×628 (`UNIT_W_N(6)`), ten 361 px columns, gap 14, tile height 189.
- Nothing is smaller than 20 px: regular 20 for labels, uptime and OS, bold 20
  for alerting labels and the drive figure, bold 28 for names and the top-core
  and RAM figures, bold 36 for the CPU and GPU figures.
- **CPU on one line.** The run is drawn right to left so it ends at the
  column's right edge: average (36, blue), a muted `"  /  "`, then top core
  (28, purple). The separate "top core" row is gone.
- **The OS name gives way to the uptime** on the shared line, and the tile's
  hostname gives way to its right-aligned uptime. Each is truncated with `..`
  against the measured width of the other.
- **Drive row** at the bottom: name (plus `  +N`) on the left, figure in bold
  on the right, an 8 px bar beneath. In an alert, all three take the alert
  colour. The name is drawn bold and the bar is lavender otherwise.
- Drive names stay fg, not the mock-up's Subtext0, keeping 029's
  no-new-Mocha-colour choice. The only new colour is Ken's top core.
- Frame radius is 10, the mock-up's `rx`; it was 8.

### Placement

`ui.c` still creates the widget at `COL_X(0), ROW_Y(0)`. Its width now comes
from `HC_W`, so row 0 holds only this widget. With seven hosts the three free
columns stay empty, per the overseer's default: what fills them is a later
brainstorm with Ken, and he wants something graphical, not Fortune. Dev
graphs, NAS, 10G and the footer did not move.

## Verification

- `just check`: green. 17 ctest targets, with `test_hostcols` grown by
  `test_fullest_drive`, plus check-release, client, units, scripts and fonts.
- `just check-warnings-full`: the whole tree compiles at `-O2 -Werror`, and
  all 18 ctest targets pass there.
- `test_widget_leak` needed two changes:
  - Its display was still 1896 wide, so half the widget rendered off-screen.
    It is now 3792.
  - After that, churn grew the heap by 17,392 bytes against an 8 KB
    tolerance. The growth was **identical at 400 and at 1600 rounds**, so it
    is a one-time fill of LVGL's caches by the larger glyphs, not a leak. The
    warm-up now also runs 100 rounds of the churn pattern before the baseline
    is taken. With that, the test passes at 400 rounds and at 1600 (checked
    by a temporary edit, then reverted).
- Its source list now links bold 20/28/36, the fonts the widget uses, in
  place of 14/16/24.
- **Offline renders** (`tests/render_host_columns`). The mock fleet now
  mirrors the new mock-up's `hostsData`: same order, every host awake, and the
  same five alert faults, including kubs0's second filling drive. Renders at
  7, 10, 11 and 20 hosts, quiet and alert, match the mock-up's layouts. They
  give 7/10/9+2/5+15 slots, `src  +1` in yellow on kubs0, NO DATA on rpi53,
  and a red `root 93%` on ksandbox's tile.

## Pre-ship deploy and the CPU number

The proposal named the CPU cost as the number that could say no, and asked
for it to be reported before the ship rather than tuned silently. So the
branch build (`62be003`) was deployed to rpi53 from kai with
`scripts/deploy.sh` at 11:08:42 PST on 2026-11-12, ahead of the clearance.
The ship turn redeploys from the merged `main`.

- The deploy reported the service active, `NRestarts=0`, and the running
  binary self-reports `62be003 (2026-11-12)`. The aarch64 build and link
  printed no warnings (`.scratch/deploy-030.log`, grepped).
- **CPU, `top` on rpi53, % of one core:**

  | build | state | kpidash CPU |
  |---|---|---|
  | old client cards (pre-029) | steady | ~48 % (029's figure) |
  | 029 host columns | rings full, 13 h uptime, just before this deploy | 36.5-36.6 % |
  | 030 full row | rings filling, 0-10 min, 30 s samples | 62 % rising to 65 % |
  | 030 full row | rings full, 10 min, 10 s samples | **65 %** |

  **That is clearly worse than the old 48 %**: about 1.8× 029 with the rings
  full. All of it is the one LVGL thread (`top -H`: 65.1 %), the same thread
  that runs the synchronous 1 s Redis poll. That leaves roughly 350 ms of each
  second for the poll and everything else. The panel renders correctly, and
  the journal shows no errors. It is visible on the panel itself: rpi53's own
  top-core line sits near 66 %.
- **Not tuned before reporting, as the proposal asked.** My guess at the
  time was the sparkline fills. The measurement below showed that guess was
  wrong.
- Live captures (`.scratch/live-030.png` full panel,
  `.scratch/live-030-row0.png` row 0) and the 7/11/20 alert and 20 quiet
  renders are attached to korg:4545. They show seven full columns across row
  0 in priority order (kai, kubs0, cleo, then gratch, kimac, kubsdb, rpi53).
  The three free columns are empty, and rows 1-2 and the footer are
  unchanged.
- Rollback: `~/kpidash.prev` holds the 029 binary (`ca9388c`, md5
  `958bd20b…`), so `scripts/deploy.sh --rollback` returns the panel to 029
  in one step. The diagnostic deploys below overwrote it; it was restored
  from a copy taken before the first one.

## Tuning (overseer ruling on korg:4545: tune in this sprint)

### Measure first

- **The kai bench could not see it.** `.scratch/bench` repaints the widget
  with seven mock hosts and full rings. It was cross-built and run on rpi53 in
  ordinary RAM: one column takes 1.45 ms and the whole widget 12.7 ms. A
  couple of column repaints a second cannot cost a third of a core. (`perf`
  on kai could not attribute it either: LVGL queues draw tasks, so the blend
  cost lands in dispatch, not in the code that queued it.)
- **Live instrumentation** (temporary, never committed). Display event
  callbacks counted invalidated areas by region and timed `REFR_START` to
  `REFR_READY`, and the 1 s poll was timed. Every 10 s on rpi53:
  - The poll plus `ui_refresh` costs 2.4 ms/s. **The refreshes cost 626
    ms/s**: two a second, about 313 ms each.
  - Invalidations: row 0 about 2 Mpx/s (each column roughly once a second),
    rows 1-2 about 66 Mpx/s (every dev graph cell whole, every second), and
    the footer about 4 Mpx/s.
  - **029 + the same instrumentation** gives the same invalidation pattern
    but **346 ms/s**. Rows 1-2 and the footer are identical code, so the
    extra ~280 ms/s is the host widget's drawing.
- **Bisected live** with a flag file read each poll, switchable without a
  restart:

  | skipped | refresh ms/s |
  |---|---|
  | nothing | 633 |
  | sparkline fills / lines / both | 628-630 |
  | all text | 630 |
  | **the column frame alone** | **98** |
  | the whole column draw | 75 |

**Finding.** Each column's frame is a 361×596 rounded rect in Surface0 at
26 % opacity. A translucent fill makes LVGL read back every pixel it covers.
On the Pi, LVGL renders `LV_DISPLAY_RENDER_MODE_DIRECT` straight into the DRM
framebuffer, and reading that memory back is slow. Each column repaint
therefore spent tens of milliseconds reading its own background. 030 doubled
that area per column, and 029 already paid the same tax at half size, which
is why its 346 ms/s was mostly this too. RAM-backed benches cannot show it.

### Fix

Mix the colour once (`lv_color_mix(Surface0, Base, 66)`) and draw the frame
**opaque**: the same pixels, with no read-back. The offline renders differ
from the pre-fix ones only at 8 anti-aliased corner pixels per column, by 1 in
the red channel. Refresh fell from 626 to 87 ms/s.

The overseer's other levers (invalidate figures and graphs separately, skip
identical text, fewer fill primitives) were **not pulled**. The bisection
shows the graphs, the fills and the text together cost nothing measurable,
and the target is met without them.

### Result

Clean build `a80d6b9` was deployed 11:33:12 PST and measured 552-612 s after
the restart, with the rings full. `top` on rpi53, % of one core, all four
numbers measured the same way:

| build | kpidash CPU |
|---|---|
| old client cards (pre-029) | ~48 % (029's figure) |
| 029 host columns, rings full | 36.5 % |
| 030 before tuning, rings full | 65 % |
| **030 tuned, rings full** | **10.6 %** |

- RSS is unchanged at 69 MB, with `NRestarts=0` and no journal errors or
  warnings. The aarch64 build and link printed no warnings.
- The live capture (`.scratch/live-030-tuned.png`) looks the same as before.
  rpi53's own column now reads 3 % / 7 % where it read 17 % / 66 %.
- After the fix: the offline renders (above), `test_widget_leak` (PASS),
  `just check` and `just check-warnings-full` (18/18) all pass.
- The same tax probably applies to translucent fills elsewhere on the panel.
  Rows 1-2 still invalidate ~66 Mpx/s, but the whole refresh now costs ~87
  ms/s, so there is nothing there worth chasing in this sprint.

## Repaired in passing

- `docs/ARCHITECTURE.md`'s palette table listed Teal as "CPU top core".
  That was stale even under 029, which drew the top core white; Teal is the
  10G fabric colour (`WS_COLOR_TEAL` in `common.h`). The row is corrected,
  and the top core has its own `#AB47BC` row.
