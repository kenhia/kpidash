# 040 — time and calendar card in row 3

Proposal korg:4668 covers WI 4665. This is slice 3 of program korg:4669
("kpidash row 3: time and calendar card"). It ran as a karc leg
(kpidash-7a52b0) on kai, with an overseer on cleo. The feed contract is
kdashdata sprint 025 (proposal korg:4666, handoff korg:4671), and korg's
publisher (korg:4667) was built alongside it.

## Goal

Ken picked card **A · Clock and month** from the calendar mock-ups on
2026-11-14, with one change: a 24-hour clock instead of AM/PM. The task was
to draw it as a 2×1 at the right end of row 3 (counting rows from one;
`ROW_Y(2)`, `COL_X(4)` in code, crop origin (2552, 1256), 1264 × 628). The
mock-up source is committed as `planning/calendar-mockups.html`, and its
`cardA`, `sideClock`, `grid`, `cell` and `chip` functions are the spec.

## Premise check

- **The contract exists.** kdashdata `0e921a1` (its main, after the sprint
  025 deploy record) has `kdash_parse_calendar`, `kdash_calendar_covers`,
  `kdash_calendar_day`, `KDASH_KEY_CALENDAR` and the schema. The submodule
  moved from `8cb64c6` to `0e921a1`. The existing 18 tests passed unchanged
  on the bump.
- **The 96 px digits font has no colon.** Confirmed: its range was
  `0x30-0x39,0x2D,0xB0,0x2014`.
- **rpi53's zone is America/Los_Angeles** (`timedatectl`, read 01:05 PST),
  and the service sets no `TZ`. So `localtime_r` on the panel gives the
  feed's zone.
- **The live key was absent.** `EXISTS kdash:calendar` returned 0 on central
  at 01:11 PST, because korg's publisher had not shipped yet.

## Decisions

- **The model is `calstate.c`, and it is pure.** libkdash parses the record
  and answers the window. calstate adds the month arithmetic, the cell flags,
  the coming-up list and the strings, and takes the clock as an argument.
  This follows the pattern of `kkstate.c` and `limstate.c`.
- **One `kdash_calendar_t`, static.** The handoff measured it at about
  5.8 KB. calstate keeps the record and one parse scratch as statics, so a
  rejected record never touches the good one. Nothing that size goes on the
  stack. A parse happens only when the value changes (one strcmp per poll).
- **The read holds a full-cap record.** hiredis allocates the whole reply,
  and calstate `strdup`s it with no length clamp. `test_calstate` builds a
  64-entry record with every string at its bound (over 9,000 bytes) and
  requires all 64 entries to be adopted. It also builds a 66-entry record
  and requires the earliest 64 to be kept.
- **Today is the local date.** "In N days" is a difference of date numbers,
  not seconds divided by 86,400, so a 23- or 25-hour DST day cannot round it
  wrong. A planted seconds-based version failed four checks.
- **Tags only while the window covers today.** When the window does not
  cover today, every cell is untagged (weekends stay grey) and the list
  reads "calendar out of date". With no record at all it reads "no calendar
  feed". A planted version that ignored `covers()` failed four checks.
- **The old-`ts` hint** is a muted "as of 3h" at the right end of the COMING
  UP line. It appears only past `expected_refresh_s` plus 60 s, and it never
  changes any tag. It is the "muted hint at most" that CD-30 allows.
- **The coming-up label** is `special`, else `holiday`, else "day off". This
  is the mock-up's own order (`t.sp||t.hol||'day off'`). `short` is not used
  on card A. The list runs past the end of the month, as far as the window
  goes (WI 4665: "Unlike the mock-up").
- **A label too wide for the column** (names can be 31 characters) is cut
  with "…" (U+2026, in the bold faces' range) so it ends at x = 456, 24 px
  clear of the grid at 480. The sub-line under it is held to the same width.
  Nothing wraps or scrolls. "Martin Luther King Jr. Day" fits uncut;
  "Independence Day (observed)" (27) is cut. This was tightened from 466 on
  the overseer's review of korg:4682, which also asked for a render and a
  check: `render_calendar long` draws four names at the bound, and it fails
  if any pixel lands in the 24 px gap. The full tree's ctest runs it as
  `render_calendar_long_names`. Lifting the limit to 600 made it report 668
  stray pixels.
- **The middle dot in "Thu · in 13 days"** comes from the bold 20 face. The
  sub-line is regular 20, and LVGL's built-in regular face has no U+00B7: it
  would draw a box and log a warning on every redraw (WI #2646). Splitting
  the line at the dot keeps the mock-up's regular weight.
- **Repaint.** The view is rebuilt on every poll and compared with
  `memcmp`. When only the clock changed (once a minute), only its
  440 × 130 rectangle is invalidated. The date turning or a new record
  repaints the whole card. Every fill is a pre-mixed opaque colour
  (sprint 030).
- **Nothing is under 20 px.** The smallest text is the 20 px day headers,
  the sub-lines and the hint.

## What shipped

- `fonts/generate.sh` and a regenerated `lv_font_montserrat_bold_96_digits.c`
  with the colon (0x3A). `check-fonts` now pins U+003A; it failed before the
  font was regenerated and passed after.
- `src/calstate.{h,c}`: the model.
- `src/widgets/calendar_card.{h,c}`: the card at 1256 × 620, in the
  mock-up's coordinates.
- `src/redis.c`: `redis_poll_calendar()`, one read-only GET per poll through
  the same `poll_status_key()` the korg + klams card uses. `src/ui.c`: the
  slot at `COL_X(4)`, `ROW_Y(2)`, always shown.
- `tests/test_calstate.c` (164 checks). It covers the schema's examples and
  counterexamples; the mock-up's 13th and 27th; midnight; a month end and a
  year end; both DST changes, including the repeated 01:30 and the
  01:59 → 03:00 jump; no feed; holding through an absent key and a rejected
  record; the old-`ts` hint; the window running out; and the full-cap record.
- `tests/render_calendar.c` (tool): `render_calendar out.bmp
  now|d27|six|nofeed|stale|outdated`, or `live calendar.json [time]`.
- Docs: ARCHITECTURE.md (data flow 16, the source tree, a "Time and Calendar
  Card" section), CLIENT-PROTOCOL.md §8j, the `check-release` file list in
  CLAUDE.md, `.github/copilot-instructions.md` and the justfile, and the
  roadmap.

## Renders

All renders are 1264 × 628, the card's whole frame. now (img-1246), d27
(img-1247) and six (img-1248) are attached to korg:4668, and so is the
scratch-Redis proof (img-1249), and long (img-124b). Every render was compared with `cardA` by its
coordinates and colours, and
with `cal-A-24h.png` and `cal-d27.png` (ignoring the AM/PM in the second).

- **now**: Friday 13 November 18:42. The 11th has a dimmed rose number
  (worked). The 25th is tinted. The 26th has a rose number on the plain cell,
  as in the mock-up's NOV, which tags it as a holiday only. The 27th is
  tinted with the yellow ring. Coming up lists the 25th, 26th, 27th and
  Christmas, so the list runs into December.
- **d27**: 09:15. The 27th is solid blue with the ring, and "Lynn's birthday,
  Fri · today" is first in the list.
- **six**: August 2026 in six shorter rows inside the same box.
- **stale**: the now record, read 3 h 5 m after its `ts`. It shows "as of 3h"
  and changes nothing else.
- **outdated**: 20 February 2027, past the window. The grid and weekends are
  untagged, and the list reads "calendar out of date".
- **nofeed**: the grid and weekends, and "no calendar feed".
- **long**: the 13th with "Independence Day (observed)", a joined 31-character
  special name, 31 W's and "New Year's Day (observed)". The first three are
  cut with "…" before the grid.

## Proof against a Redis

The live key was absent, so the tags were proved against a **scratch Redis**
(`redis:7-alpine` in Docker on kai, `127.0.0.1:16399`, removed afterwards).
The schema's first example, with `ts` set to the moment, was `SET` there,
read back with `GET` and rendered `live` at that time (01:11 PST,
14 November). The render shows the 11th, 25th, 26th and 27th exactly as the
record says, with "Ken's birthday" in the list. **Nothing was written to
central.** The hiredis read on the panel is the `poll_status_key()` path that
has carried the korg and klams keys since sprint 038. The overseer does the
final live capture once korg's publisher is writing.

## Gate

- `just check` passes. ctest runs 19/19, including `test_calstate`;
  `check-release` builds `calstate.c` at `-O3 -Werror`; and the client
  (159), units, scripts and fonts (24) checks pass.
- `just check-warnings-full` builds the whole tree at `-O3 -Werror`,
  including `calendar_card.c`, `ui.c`, `redis.c` and `render_calendar`, and
  its ctest passes 21/21, including `render_calendar_long_names`.
- The first `-O3` build caught three `-Wformat-truncation` warnings in
  calstate's date, age and day-count strings. They were fixed before
  anything was committed by bounding each field with a modulo.
- The aarch64 Release cross-build (`build-pi`, as `deploy.sh` builds it)
  printed **zero** warnings, and the link printed nothing.

## Pre-ship reading

Panel CPU before the ship was **7.2 % of a core**. This is
`kpidash.service` pid 291964, `utime + stime` over 120 s, ending 01:15 PST
on 2026-11-14, read from `/proc` on rpi53 over ssh from kai. No second
process was started. The acceptance compares the post-deploy reading against
this figure.

## Repaired in passing

- **The roadmap's "Now" still led with sprint 036.** 037 to 039 had shipped
  without updating it. It now leads with 040 and folds 036 to 039 into the
  shipped list. Gate: none (prose), checked by reading.

## Left for the ship turn

- Deploy with `scripts/deploy.sh`, and read the aarch64 link output by eye.
- A `krpidss` capture of row 3 cropped to the card, checked against the
  mock-up. It should show the real tags if korg's publisher is live, or an
  untagged grid and "no calendar feed" if it is not.
- Panel CPU after the deploy, measured the same way, against 7.2 %.
