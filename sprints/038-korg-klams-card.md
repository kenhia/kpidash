# 038 — korg + klams card in row 2

Proposal korg:4618 covers WI 4613. Slice 5 of program korg:4619 ("kpidash
row 2: Claude limits card and korg + klams card"). It ran as a karc leg
(kpidash-86a779) on kai under an overseer on cleo. The proposal is titled
"Sprint 038", which matches this time.

## Goal

Draw korg and klams sharing one 1×1 in row 2, column 4 (counting rows from
one; `ROW_Y(1)`, `COL_X(4)` in code), to the "KORG + KLAMS" mock-up Ken picked
on 2026-11-13 (`planning/row2-cards-mockups.html`, function `combo()`). korg
is the top half and klams the bottom.

Where WI 4613 and the overseer's comment 4672 on the proposal disagree, the
comment wins. It carried facts forward from the contract slice (korg:4615)
and the two publisher slices (korg:4616, korg:4617), and the scanner rows are
the main thing it changed.

## Premise check

- **The contracts exist.** kdashdata main has `kdash_parse_korg_pulse`,
  `kdash_parse_klams_status`, `kdash_cadence_stale` and `KDASH_CADENCE_GRACE_S`
  (sprint 023, `d57e1dc`). The submodule moved from `01253fd` to `8cb64c6`,
  which also carries CD-29 for `claude:limits` (sprint 024). The existing gate
  passed unchanged on the bump: 17/17, and `test_limstate` was not affected.
- **kpidash links `kdash_core` only**, so `kdash_korg_pulse()` and
  `kdash_klams_status()` from comment 4672 are out of reach: they live in
  libkdash's I/O shell. The panel does its own `GET` and calls the parsers.
  It has done the same for every other feed.
- **Feeds at start (01:41Z).** Both keys held records, but both were about 20
  minutes old, against 6-minute and 2-minute windows. By 01:56Z korg's timer
  was writing (age 155 s). klams's record stayed stale through the whole
  working turn: `klams-monitor` on kubs0 was still the process started on
  2026-11-09. Its slice (korg:4617) was cleared to ship at 01:34Z, and that
  clearance includes the restart. Both records were saved as test fixtures.
  They are valid against the contract whatever their age.
- **klams went live at 17:48:33 PST**, when that ship restarted klams-monitor.
  Its first two records were a fresh `down`: the same deploy had restarted
  klams-service, and `/healthz` refused the connection. From 17:49:49 it read
  `ok`. The publisher behaved exactly as CD-27 asks, and the transient shows
  why the debounce exists. A panel reading that key through the restart would
  not have blanked the half for one record. On its first-ever record it would
  have, by design: with no earlier record, there is nothing to keep.

## Decisions

- **libkdash parses and judges age; `kkstate.c` adds the rest.** The rest is
  the debounce, the scanner verdict, the 30-day line's points and the strings.
  It is pure and gate-tested, like `limstate.c`.
- **Scanner rows follow comment 4672.** A healthy scanner, whose
  `last_ran_at` is within its interval plus 60 s, drains its bar from
  `last_ran_at` in sapphire. Its write age is secondary text in Subtext0
  ("wrote 3d ago"). A stale `last_ran_at` means the scanner has stalled: the
  bar is outlined in red with no fill, and "ran 5h ago" is in bold red. A null
  `last_ran_at` (kai today, klams WI 4639) is neutral, never red: a write-age
  bar in sapphire mixed halfway to the card colour, so it reads as a value and
  not a verdict. The mock-up's red "kubs0 3d ago" row does not appear: kubs0
  is healthy.
- **The debounce covers klams's own verdict as well as the subsystem dots.**
  WI 4613 asks that a subsystem dot turn red only after the same miss appears
  on consecutive records. klams's `degraded` comes from that same miss, so a
  yellow health dot beside four green subsystem dots would contradict itself.
  `degraded` and `down` are therefore held to the same two-record rule.
  - A single `down` record is not adopted at all. Its values are all null,
    and blanking the half for one record is the flash sprint 035 forbids.
  - Recovery shows at once.
  - A record counts as new when its `ts` changes. Polling the same record
    twice does not count as two.
- **A fresh `down` is different from a stale record**, as comment 4672 asks.
  A fresh `down` shows a red dot and "down" in red at the right, with no
  values. Drawing blanks instead would look like zero, so the half says it.
  A stale record gets the grey treatment, keeps its last values, and shows
  "no data 14m" in yellow where the dot was. A dot from an old record would
  claim knowledge it does not have.
- **Each half is stale on its own feed, and the card is always shown.** A
  half whose feed has never published draws its title in grey and "no feed",
  so the other half never moves. This replaces the hidden-until-first-record
  pattern of the limits and NAS cards, because the proposal asks for it
  explicitly.
- **The 30-day line is placed by date, not by index.** Missing days are
  absent on the wire, and with index placement a gap would slide every later
  day left. The span is the 30 days ending at the newest entry, with y
  running from 0 to the series maximum, as the mock-up draws it. Fewer than
  two points draws no line. The line also moves right of a three-digit
  count: the mock-up's x = 300 assumes two digits, and korg read 165 on
  2026-10-24.
- **Project bar.** The first segment is lavender, the next three are lavender
  mixed halfway to the card colour, and the rest are Surface1, as in the
  mock-up. When `other_open` is above zero it is one last Surface0 segment:
  the remainder recedes. The top project's name is cut with ".." until it
  clears the lane counts. Names can be 47 characters long, and "homelab-health
  12" would otherwise run into "active".
- **Two scanner rows at most.** The cell has room for two, and klams has
  two. A third would be dropped, not squeezed in. The contract allows eight.
- **Repaint per half.** The scanner bars lose a pixel about every 14 s, and
  that must not repaint korg's line, so the card invalidates only the half
  that changed. Everything is opaque (sprint 030), including the line's area
  fill, which is drawn one opaque column per pixel.

## What shipped

- `src/kkstate.{h,c}`: the pure model. It covers observe (with the
  unchanged-value strcmp), staleness for each half, the debounce,
  `kk_scan_view`, `kk_korg_spark`, and the count, duration and "no data"
  strings.
- `src/widgets/korg_klams_card.{h,c}`: the card at 624 × 620, drawn in the
  mock-up's coordinates.
- `src/redis.c`: `redis_poll_korg_klams()`, one read-only `GET` per key per
  poll. `src/ui.c`: the slot at `COL_X(4)`, `ROW_Y(1)`, always shown.
- `tests/test_kkstate.c` (143 checks). Both schemas' examples must be
  adopted, and every x-counterexample must be rejected, skipped or truncated.
  It also covers the two live records, the three scanner states at their
  boundaries, the debounce (a single blip, a confirmed miss, a repeated poll,
  `down` single and confirmed, a first-ever `down`), the line across a month
  end, gaps and dropped dates, and the strings.
- `tests/render_korg_klams.c` (tool): `render_korg_klams out.bmp
  ok|warn|stale|down|nofeed`, or `live korg.json klams.json [now]`.
- Fixtures: `tests/fixtures/korg-pulse-2026-11-14.json` and
  `klams-status-2026-11-14.json`.
- Docs: ARCHITECTURE.md (data flow 15, the source tree, a "korg + klams Card"
  section), CLIENT-PROTOCOL.md §8i, the `check-release` file list in
  CLAUDE.md, `.github/copilot-instructions.md` and the justfile, and the
  roadmap.

## Renders

All renders are 632 × 628, the whole cell, and are attached to korg:4618.
Compared with `combo()` by its coordinates and colours. kai has no SVG
rasteriser (no chromium, rsvg or playwright), so the mock-up was read rather
than rasterised.

- **ok**: the mock-up's D.ok data. 25 open with its line falling from 118,
  "1 awaiting Ken" in peach, "0 active", "kligd 5" and a 16-segment bar.
  228,934 chunks with a green dot. kai ran 9 minutes ago (bar 0.85 full) and
  wrote 19 minutes ago. kubs0 ran 3 minutes ago and is healthy, with "wrote
  3d ago" as secondary text. Four green subsystems.
- **warn**: D.warn. 41 open with a rising line, "4 awaiting Ken" in peach, "2
  active" and "k-homelab 9". The klams dot is yellow, kai has stalled ("ran 5h
  ago" in red with the red outline), and rerank is red after two records.
- **stale**: the ok records 14 minutes on. Both halves are grey, each with
  its own "no data 14m".
- **live**: the records read from rpi53 at 17:49:54 PST, drawn at that
  moment. korg 256 s old: 35 open, "1 awaiting Ken", "2 active", "kligd 5",
  and an 18-segment bar with no `other_open`. klams 20 s old: ok, 229,318
  chunks. kai is neutral (no `last_ran_at`, its write-age bar half drained,
  "wrote 30m ago"). kubs0 is healthy (ran 39 minutes ago, "wrote 3d ago").
  Four green subsystems. The first live render showed "wrote 30m ago" touching
  the bar, so the bar now leaves the text a 210 px column.
- Not attached, but rendered: **down** (korg fresh; klams dot red, "down", no
  rows) and **nofeed** (both titles grey, "no feed").

## Gate

- `just check` passes: 18/18 ctest including `test_kkstate`, and
  `check-release` builds `kkstate.c` at `-O3 -Werror`. The client, units,
  scripts and fonts checks also pass.
- `just check-warnings-full`: the whole tree at `-O3 -Werror`, including
  `korg_klams_card.c`, `ui.c`, `redis.c` and `render_korg_klams`, builds
  clean. Its ctest passes 19/19.
- The first `-O3` build caught a `-Wformat-truncation` in the scanner text
  buffer and a `NAME_MAX` clash with `<limits.h>`. Both were fixed before
  anything was committed. This is the class sprint 037 moved the gates to
  `-O3` to catch, and it worked.

## Pre-ship reading

Panel CPU before the ship: **7.3 % of a core**. That is `kpidash.service`
pid 284797, `utime + stime` over 120 s, ending 17:46 PST on 2026-11-13,
read from `/proc` on rpi53 over ssh from kai. No second process was started
(see sprint 037). The acceptance compares the post-deploy reading against
this. Sprint 037 measured 6.7 % before its ship and 5.0 % after.

## Left for the ship turn

- `just check-warnings-full`, then deploy with `scripts/deploy.sh`. Read the
  aarch64 link output by eye.
- A `krpidss` capture of row 2 showing 10G, NAS, this card and the limits
  card, with live numbers checked against korg's board and klams-view at
  capture time.
- Panel CPU after the deploy, measured the same way, against 7.3 %.
