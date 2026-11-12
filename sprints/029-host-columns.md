# Sprint 029: host columns

Proposal korg:4527 covers WI 4523, 4524, 4525 and 4526. It came out of Ken's
dashboard-refresh brainstorm on cleo (2026-11-11), where he picked "C ·
Columns" and "E · Quiet fleet" from `planning/refresh-mockups.html` and the
overseer drew the pixel spec as `planning/hosts-columns-mockups.html`. It ran
as karc leg `kpidash-581492` on kai, headless, under `/overseen-sprint`.
Ken's two colour changes (WI 4524/4525 comments) override the mock-up file,
which was deliberately not redrawn: the top-core line, its figure and the
tile's top-core tick are fg white, not sapphire, and uptime is fg, not muted.

## Premise check

| claim | verdict |
|---|---|
| client_info_t already carries cpu, top core, RAM, GPU, disks, uptime, OS, intermittent, online | **holds** (`src/registry.h`) |
| KPIDASH_PRIORITY_CLIENTS is parsed and ordered; live value in `ops/rpi53/dashboard/config.env` | **holds**: `kai,kubs0,cleo` |
| MAX_CLIENTS and PRIORITY_CLIENTS_MAX are 16 | **holds** |
| the panel keeps no host-telemetry history | **holds**: only the dev graphs keep a chart |
| ui_refresh snapshots MAX_CLIENTS client_info_t on the stack | **holds**, so it moved to a static buffer |
| client_card.c may encode different drive thresholds | **it does**: orange above 60 %, red above 75 %. Kept, see below |

## Decisions

### History ring (WI 4523): `src/hosthist.{c,h}`

- **150 samples × 4 s = 10 minutes**, one sample per pixel of the 147 px
  sparkline. Several readings inside one 4 s slot: the latest wins.
- **Recorded in `redis_poll` and only when the telemetry key was present
  this cycle.** That flag is the honest freshness signal: a host that stops
  reporting is never recorded again, so its trace holds at the last thing it
  said and never draws a drop to zero.
- **Gaps.** Up to 3 missing slots (12 s, inside the telemetry key's 15 s TTL)
  hold the previous value, which covers a missed poll. A longer gap means the
  host was away and is not filled. A flat line across an absence would claim
  data nobody sent.
- Its own mutex, a table bounded by MAX_CLIENTS, and the least recently
  recorded host gives up its ring when the table is full. Not persisted, as
  the WI accepts.

### Pure model (WI 4524/4525): `src/hostcols.{c,h}`

All decisions live here and are unit-tested. The widget only draws.

- **Fill rule** `hostcols_plan`: n ≤ 10 is all full columns. Otherwise
  `full = (30 − n) / 2` and the rest are tiles, filled column-major. The
  table test pins Ken's checks (11 → 9 + 2, 20 → 5 + 15, 30 → 0 + 30), the
  cap past 30, and that every n fits in ten columns.
- **Order** = demotion order: priority list first in list order, then
  hostname (`qsort`). Registration order no longer leaks in, and the test
  feeds reversed input to prove it.
- **Who gets a slot** `hostcols_select`: an intermittent host that is asleep
  gets none (the overseer's default 1, which stands). Past 30, the first 30
  by priority are shown, and `ui.c` logs the count left out whenever it
  changes, not every second.
- **Attention** is colour only. RAM uses the mock-up's ≥ 80 / ≥ 90. **Drives
  keep the retired card's > 60 warn / > 75 crit**, because WI 4524 says to
  keep the card's own thresholds when it had different ones. The alert render
  shows the consequence: kubs0's `src` at 86 % is red, where 80/90 would make
  it yellow. CPU, GPU and VRAM never raise attention. An always-on host
  that stops reporting is `NO DATA <age>` with a red border, its metrics
  dimmed at their last values.
- **Drives**: shown as reported, unless there are more than five. Then the
  five fullest are shown, fullest first, so the drive that matters is never
  the one cut. The tile shows the fullest drive, its name cut to five
  characters.

### Widget (WI 4524/4525): `src/widgets/host_columns.{c,h}`

- **Custom draw, not lv_chart.** Each of the 30 slots is one style-less
  `lv_obj` painted in `LV_EVENT_DRAW_MAIN` from a pre-formatted model: text,
  bars, sparklines (a 1 px-column area fill plus `lv_draw_line` segments).
  All 30 are created once. An update only moves, resizes and hides objects,
  and it invalidates a slot only when its model or history changed
  (`memcmp`). Nothing is allocated per update, so there is nothing to leak.
  That is the sprint-005 lesson applied by construction, and the leak test
  checks it.
- **No new colour or font.** The column fill is Surface0 at 26 % over the
  card's Base, which lands on #232335 against the mock-up's raised #232336.
  The mock-up's Subtext0 (drive names, VRAM figure) is not in `common.h`, so
  those are fg.
- Text is placed by **baseline**, the way the SVG places it, and truncated
  with `..` rather than wrapped. A long OS string like "Win 11 Pro 26H2
  26300.9818" ends in `..` instead of drawing over the uptime.

### Placement (WI 4526)

- Row 0 columns 0-2. **Row 0 columns 3-5 stay empty** (the overseer's default
  2, which stands). Rows 1-2 and the footer are untouched.
- Removed: `src/widgets/client_card.{c,h}`, the `g_card_grid` flex container
  and its card bookkeeping in `ui.c`, and `resources/` (hdd/nvme/ssd PNGs).
  Nothing in the code referenced them; only stale doc lines did, and those
  are gone too.
- MAX_CLIENTS and PRIORITY_CLIENTS_MAX went from 16 to 32. Every array sized
  by them was checked: the admitted set, netstate, `dev_cmd_state_t`, main.c's
  seed buffers and redis.c's SMEMBERS cap. All are static or small. The one
  large stack user, ui_refresh's `client_info_t` snapshot, is now static.

## Verification

- `just check`: green. 17 ctest targets, including the new `test_hosthist`
  and `test_hostcols`, plus check-release, client, units, scripts and fonts.
- `just check-warnings-full`: the whole tree compiles at `-O2 -Werror`.
- `test_widget_leak` (full tree only) gained `test_host_columns_churn`:
  every layout from 0 to 30 hosts, then 400 rounds of changing models with a
  real render pass each round. Heap growth measured with `mallinfo2()`
  (LVGL uses the C allocator here) stays under the existing 8 KB tolerance,
  and destroying the widget returns its memory. Passes.
- **Seeded fake hosts, offline.** `tests/render_host_columns` (a tool built
  with the full tree) renders the widget to BMP. It uses either the
  mock-up's own fleet (`hostsData`, same order, same asleep hosts, same five
  alert faults) or a host file. Renders at 10, 11, 20 and 30 hosts, plus
  10/20 in the alert state, match the mock-up's layouts. A render of the live
  fleet, read from rpi53's Redis, shows kai, kubs0 and cleo first, then
  gratch, kubsdb and rpi53. All seven images are attached to the proposal
  (korg nodes 4528-4534). I deliberately did not use `scripts/load_test.py`
  against the live Redis: `kpidash:clients` is append-only and the panel
  persists admitted hosts, so fake hosts seeded there would stay on the panel
  for good.

## Follow-ups

- What fills row 0, columns 3-5 is Ken's call (roadmap "Next").
- The drive thresholds are a ruling the overseer may want Ken to confirm: the
  WI's own rule kept the card's 60/75, while the mock-up drew 80/90.
