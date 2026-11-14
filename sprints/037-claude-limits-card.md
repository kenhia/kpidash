# 037 — Claude limits card in row 2

Proposal korg:4614 covers WI 4609. Slice 1 of program korg:4619 ("kpidash
row 2: Claude limits card and korg + klams card"). It ran as a karc leg
(kpidash-6c6a4c) on kai under an overseer on cleo. The proposal is titled
"Sprint 036", but 036 was already taken by the client's 2.0.0 release, so
this is 037. That makes the program's slice 5, titled "Sprint 037", 038.

## Goal

Draw the Claude limits card Ken picked on 2026-11-13 as a 1×1 in row 2,
column 5 (counting rows from one; `ROW_Y(1)`, `COL_X(5)` in code). It shows two
rings for the 5-hour and 7-day windows, each with a pace tick, and a bar for
the model-scoped weekly window. Normal state is peach, 75 % and above yellow,
90 % and above red. Column 4 stays empty until the korg + klams card (WI 4613)
lands. The source of truth is the mock-up, committed here as
`planning/row2-cards-mockups.html` (card "CLAUDE LIMITS", function `limits()`)
with `planning/row2.png`, the live row it is drawn beside.

## Premise check

The proposal asked for one check before building: does the Redis the panel
reads hold `claude:limits`? **It does, on the same connection.**

- kdashdata's `KDASH_CLAUDE_REDIS` stem answers `rpi53:6379` (architecture.md,
  since the CD-7 cutover).
- rpi53 runs **one** `redis-server`, bound `0.0.0.0:6379`. The dashboard
  reads it at `127.0.0.1:6379`: `KPIDASH_REDIS_HOST` is unset in
  `/etc/kpidash/config.env`, so the default applies.
- `EXISTS claude:limits` over `127.0.0.1` on rpi53 returned 1, and
  `HGETALL claude:limits` from kai returned a live record (host kai, source
  oauth, 47 s old, scoped model Fable). This was a read only; nothing was
  written to the key.

So no second connection. The coupling is real, though, and it is now written
down (CLIENT-PROTOCOL.md §8h): if the claude family ever moves off rpi53, this
card needs a second connection. kpidash links `kdash_core` only, not the
libkdash I/O shell that resolves stems.

## Decisions

- **libkdash parses and judges staleness; the panel adds the rest.**
  `kdash_parse_claude_limits`, `kdash_claude_limits_stale` and
  `kdash_claude_limits_scoped_stale` (at libkdash's own 60 s grace and 1 h
  legacy window) are called as they are. `limstate.c` adds only what CD-16
  leaves to the panel: the bands, the pace tick and the strings. `host` is
  not in libkdash's struct, so limstate reads it from the field list and
  checks it against the schema's pattern. It is display only.
- **The pace tick has no position for an inconsistent record.** It sits at
  `1 − (resets_at − now) / window`. With no reset time, or a reset further
  away than the window is long, there is no tick at all, rather than one at
  twelve o'clock claiming the window just began. The live record hit this
  case on its first read (see below).
- **Band on the percentage as drawn.** 74.6 is drawn "75", so it is yellow.
  A colour that disagrees with its own number would be the visible bug.
- **Anti-flicker, carried one step further than libkdash.** An absent key or
  a rejected record keeps the last record, as the weather card does. And a
  write that carries no scoped set keeps the last scoped set. That happens
  every time a statusline write follows an oauth poll, since only the poll
  supplies the scoped fields. Without the carry, the bar would vanish and the
  card reflow on every statusline write. With it, the bar keeps its number
  and greys by its own stamp, which is exactly the per-gauge rule. The bar is
  left out only when no scoped set has ever been seen.
- **Card frame follows the mock-up, not the NAS card.** The mock-up draws
  Base (`#1E1E2E`) at radius 18, 7 px into the cell, with no border. The NAS
  card uses Mantle with a 2 px Surface1 frame at the cell pad. Ken picked from
  the mock-up, so the mock-up wins. When the korg + klams card lands, the two
  row-2 cards will match each other.
- **One object, opaque draws** (sprint 030's lesson). The card is a single
  callback-drawn object, like the weather card. Ring tracks are Surface0,
  everything opaque; the only blending is LVGL's anti-aliased edges. Any
  change to the body repaints the card: a number, a tick's half-degree step,
  a reset string's minute, or a stale flip. The header age moves every
  second, so it repaints only its own strip.
- **Header in stale state** follows the mock-up: the title greys and the
  right side reads "no data 22m" in bold yellow.

## What shipped

- `src/limstate.{h,c}`: the pure model. Parse through libkdash, observe with
  the carry, per-gauge stale, `lim_pct`, `lim_band`, `lim_pace`,
  `lim_fmt_reset` ("resets in 2h 14m" / "resets Mon 09:00" / "resets now" /
  "reset unknown"), `lim_fmt_dur`, `lim_fmt_header`, `lim_fmt_scoped_label`.
- `src/widgets/limits_card.{h,c}`: the card at 624 × 620, drawn in the
  mock-up's coordinates. The regular 20 px font has no "·", so the header
  draws it in bold 20, the weather card's runs trick.
- `src/redis.c`: `redis_poll_limits()`, one `HGETALL claude:limits` per poll,
  read only. `src/ui.c`: the slot at `COL_X(5)`, `ROW_Y(1)`, hidden until the
  first record and never hidden again.
- `tests/test_limstate.c` (97 checks), on the live record as fixture: parse,
  rejection, host check, observe/gap/reject, the scoped carry, stale per gauge
  (including a stampless scoped set and the legacy window), rounding and
  bands at 74/75/89/90, pace (including the live record's inconsistent five-hour
  reset), reset and age strings in America/Los_Angeles.
- `tests/render_limits.c` (tool): `render_limits out.bmp
  ok|warn|stale|bare|live`. ok, warn and stale are the mock-up's data with
  reset times chosen so each tick lands where the mock-up draws it. bare is a
  statusline write that never had a scoped set. live is the record read off
  rpi53.
- Docs: ARCHITECTURE.md (data flow 14, the source tree, a "Claude Limits
  Card" section), CLIENT-PROTOCOL.md §8h, and the `check-release` file list in
  CLAUDE.md, `.github/copilot-instructions.md` and the justfile, which now
  includes `limstate.c`.
- `sprints/planning/row2-cards-mockups.html`, `row2.png`.

## Renders

Rendered at 632 × 628 (the whole cell) and attached to korg:4614: ok
(img-120c), warn (img-120d), stale (img-120e). Against the mock-up:

- **ok** matches `r2b-ok.png`'s limits card: 42 % / 61 % peach, ticks at 0.55
  and 0.72, "resets in 2h 14m" and "resets Mon 09:00", and Fable at 38 % with
  its tick at 0.72.
- **warn**: 91 % red, 78 % yellow, Fable at 84 % yellow, header "kai · 2m ago".
- **stale**: everything grey, the ticks Surface1, "no data 22m" in yellow.
  The five-hour reset reads "1h 52m" where the mock-up's still says "2h 14m",
  because the render advances the clock the 22 minutes it claims.

## Observed, not this repo's

The live record kai published at 00:42Z has `five_hour_resets_at ==
seven_day_resets_at` (five days out) and equal percentages, which no five-hour
window can say. The writer is kdeskdash's `publisher/claude-pub.sh`. Filed as
kdeskdash WI 4623, which names its decision: parse fault, or API behaviour
with no open window, and if the latter, what the writer should publish. Until
it is settled, the card draws no five-hour tick for such a record but still
prints the reset it was given.

## Gate

- `just check`: 17/17 ctest including `test_limstate`. `check-release` builds
  `limstate.c` at `-O2 -Werror`. The client, units, scripts and fonts checks
  all pass.
- `just check-warnings-full`: the whole tree at `-O2 -Werror`, including
  `limits_card.c`, `ui.c`, `redis.c` and `render_limits`, builds clean.
  Its ctest passes 18/18, including the LVGL leak test.

## Repaired in passing

Nothing. The gate was green before and after.

## Pre-ship reading

Panel CPU before the ship: **6.7 % of a core**. That is kpidash pid 249172,
`utime + stime` over 120 s, from 16:44 PST on 2026-11-13, measured on rpi53
itself. The acceptance compares the post-deploy reading against this.

A mistake made during that measurement, recorded so it is not repeated: the
same ssh command ran `/home/ken/kpidash --version`. The binary has no such
flag. It started a second dashboard as `ken` without `REDISCLI_AUTH`, failed
its Redis round trip (NOAUTH) and sat there until it was killed about 95 s
later. `kpidash.service` (pid 249172) ran on untouched throughout; its
journal has a memstat line every minute with no gap. The running version is
read through Redis, as `scripts/deploy.sh` does, not from a flag.

## Left for the ship turn

- Deploy with `scripts/deploy.sh` after `just check-warnings-full`, and read
  the aarch64 link output by eye (the linker class neither gate sees).
- `krpidss` capture of row 2: the card at column 5 with live numbers.
- Panel CPU after the deploy, measured the same way, against 6.7 %.
