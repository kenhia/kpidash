# 039 — Limits card: 7-day and Fable rings when there is no 5-hour window

Proposal korg:4635 covers WI 4631. Slice 8 of program korg:4619 ("kpidash
row 2: Claude limits card and korg + klams card"). It ran as a karc leg
(kpidash-532fb0) on kai under an overseer on cleo.

## Goal

Ken's ruling, 2026-11-13: his plan no longer has a five-hour window. When the
record says so, the limits card's two rings are the 7-day limit and the
model-scoped (Fable) limit. When a five-hour window is present, the card stays
as sprint 037 built it. Add the mode choice with a debounce, the second
layout and render tests. No mock-up was drawn for the second layout, so it is
built from WI 4631's description, keeping 037's rings, colours and type sizes.

## Premise check

- **The contract exists and is pinned.** kdashdata `d8c40a4` (CD-29) added
  `kdash_claude_limits_t.five_hour_none`. The `lib/kdashdata` submodule is
  already at `8cb64c6`, which includes it. The parser zeroes the five-hour
  fields when the flag is set and sets it only on exactly `1`.
- **The live record carries it.** `HGETALL claude:limits` from kai at 02:48Z
  (read only) showed `five_hour_none 1`, `five_hour_pct 0`,
  `five_hour_resets_at 0`, 7-day 11 % and Fable 13 %. That record is now the
  `none_*` fixture in `test_limstate.c` and the `live` render.
- **The running version is `972a45d`** (`kpidash:system:version`, read from
  Redis), which is main. The panel today draws a "0 % / reset unknown"
  5 HOUR ring, which is what this sprint replaces.

## Decisions

- **Three modes, one pure function.** `lim_mode_of(record)` returns FIVE
  (037's layout), SCOPED (7 DAY and the scoped window as rings) or SEVEN (one
  7 DAY ring). Only a valid record with `five_hour_none` is "no window". An
  invalid record, or one without the flag (a pre-CD-29 writer: komarchy and
  kbook2 still run one), has a window. That is comment 4682's rule.
- **The debounce is sprint 035's rule, at its count.** The layout drawn,
  `lim_mode()`, changes only after `LIM_MODE_STREAK` = 3 consecutive new
  records give the same different answer. One agreeing record restarts the
  count. A record is "new" when its `updated_at` moved, so the 1 s poll
  re-reading one record casts one vote, as 035 keyed on `ts`. A record already
  stale when read casts no vote. An absent key or a rejected record casts none
  either, and does not reset a streak in progress. Two writers alternating
  record by record never move the card (tested).
- **The first record sets the mode outright, stale or not.** Before it there
  is no drawn layout to flap from, and the card has to be drawn some way.
- **The vote runs after the scoped carry.** A statusline write with no scoped
  set inherits the last set (037's carry), so it votes SCOPED, not SEVEN. The
  right ring keeps its number and greys by its own stamp, as comment 4682
  requires. SEVEN is reached only when no scoped set was ever seen.
- **`lim_observe` takes `now`.** It needs it for the stale-record rule. The
  one caller (`redis_poll_limits`) passes `time(NULL)`, the same clock
  `ui.c` passes to the card.
- **The freed height is used by centring, not by bigger rings.** The brief
  says to keep 037's rings. The ring group (ring top 109 to reset line 441)
  moves down 50 px (`NOFIVE_DY`), so it sits centred between the header and
  the caption: about 83 px above and 90 px below. The single ring is also
  centred horizontally (x 316).
- **"The line under the rings" read as the 2 px divider rule.** That rule and
  the bar go. The caption "white tick: how far through the window" stays,
  because both rings still carry ticks and it explains them. This is an
  interpretation of WI 4631, flagged to the overseer.
- **The scoped ring label is "FABLE · 7 DAY"**, in the rings' bold 24 with
  their letter spacing. Bold 24 carries "·" (0xB7 is in the font's RANGE), so
  it draws whole. The model is a display string of up to 31 bytes, and the
  schema's own example, `claude-opus-5`, does not fit with the window beside
  it. The label falls back to the model alone, then to the model clipped with
  "…" (never inside a UTF-8 sequence), so it never reaches the 7 DAY label.
  The `nofive-long` render shows the fallback.
- **Per-gauge staleness, unchanged rules.** In the no-window layout the left
  ring greys on the headline stamp and the right ring on the scoped stamp.
  The header follows the headline, as before.

## What shipped

- `src/limstate.{h,c}`: `lim_mode_t`, `LIM_MODE_STREAK`, `lim_mode_of`,
  `lim_mode`, the vote inside `lim_observe` (which now takes `now`), and
  `lim_fmt_scoped_ring_label`.
- `src/widgets/limits_card.c`: the body carries the mode, a label and a
  staleness flag per ring. In SCOPED and SEVEN modes it draws no bar and no
  rule and shifts the rings `NOFIVE_DY`; SEVEN draws one ring centred. The
  label-fit rule is `scoped_ring_label`.
- `src/redis.c`: passes the clock to `lim_observe`.
- `tests/test_limstate.c`: 97 → 150 checks. `test_mode_of` (the flag, its
  absence, a stated 0, no scoped set, invalid), `test_mode_first_record`
  (fresh and stale), `test_mode_debounce` (one record polled five times is
  one vote, the third record switches, two writers alternating never switch,
  an agreeing record restarts the count), `test_mode_not_on_stale_or_missing`
  (four stale records do not switch; a streak survives a gap and a rejected
  record), `test_mode_scoped_and_seven` (the carry keeps SCOPED; SEVEN to
  SCOPED after three records), and the ring label including the schema's
  long example. A mutation check: at a streak of 1, six checks fail; without
  the stale-record rule, two fail.
- `tests/render_limits.c`: the states `nofive-ok`, `nofive-warn`,
  `nofive-stale`, `nofive-long`, `seven` and `live` (the record above, read
  47 s on). 037's live record is kept as `live037`. The field table went from
  16 to 20, since the live record now has 17 fields.
- Docs: ARCHITECTURE.md (data flow 14, the card section: the no-window layout
  and which layout is drawn) and CLIENT-PROTOCOL.md §8h.

## Renders

Rendered at 632 × 628 (the whole cell) and attached to korg:4635:

- **nofive-ok** (img-122e): 7 DAY 61 % and FABLE · 7 DAY 38 %, both peach,
  both ticks at 0.72, "resets Mon 09:00". No bar, no rule; the rings are
  centred.
- **nofive-warn** (img-122f): 7 DAY 78 % yellow, Fable 91 % red, "kai · 2m
  ago".
- **nofive-stale** (img-1230): both rings grey with Surface1 ticks, the title
  grey, "no data 22m" in yellow.
- **seven** (img-1231): the single 7 DAY ring, centred.
- **nofive-long** (img-1232): the schema's `claude-opus-5`, labelled
  "CLAUDE-OPUS-5" without the window.
- **live** (img-1233): the 02:44Z record. 7 DAY 11 %, FABLE · 7 DAY 13 %,
  "resets Thu 03:59", ticks at about a third of the way round.

The 037 states (ok, warn, stale, bare, live037) render unchanged.

## Gate

- `just check`: ctest 18/18, with `test_limstate` at 150 checks.
  `check-release` (`-O3 -Werror`), client (159 passed), units, scripts and
  fonts all pass.
- `just check-warnings-full`: the whole tree at `-O3 -Werror`, including
  `limits_card.c`, `redis.c` and `render_limits`, builds with zero warnings.
  Its ctest passes 19/19, including the LVGL leak test.

## Pre-ship reading

Panel CPU before the ship: **7.6 % of a core**. That is kpidash pid 289663,
914 ticks of `utime + stime` at 100 Hz over 120 s, from 18:08:58 PST on
2026-11-13, read from `/proc` on rpi53 itself. Nothing was started there.
The acceptance compares the post-deploy reading against this. (037 measured
6.7 % before its ship and 5.0 % after; the panel varies by a couple of points
between readings.)

## Repaired in passing

None. The gates were green at the start and stayed green.

## Left for the ship turn

- Deploy with `scripts/deploy.sh` after `just check-warnings-full`, and read
  the aarch64 link output by eye.
- `krpidss` capture of row 2: the card in the mode the live record calls for.
  Today that is SCOPED (`five_hour_none 1`): 7 DAY and FABLE · 7 DAY rings.
  The new binary starts fresh, so its first record sets the mode at once.
- Panel CPU after the deploy, measured the same way, against 7.6 %.
