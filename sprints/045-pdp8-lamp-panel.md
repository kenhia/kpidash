# 045 — PDP-8/I lamp panel and rotation between panels

Proposal korg:4784 covers WI 4783. It ran as a karc leg (kpidash-4ab0a6) on
kai under an overseer. Branch `045-pdp8-lamp-panel`.

## Goal

Ken liked sprint 044's Altair panel and wanted a second machine: a PDP-8/I
front panel with its lamps in red, yellow, green and blue, with the panel
rotating between the machines. Each machine stays on for a period read from
a Redis key, 15 minutes by default, or about a minute for a fast switch.

The specification is the animated mock-up Ken approved,
`sprints/planning/pdp8-mockups.html` (copied in from
`kai:/scratch/kpidash-pdp8/` with `pdp8-widths.png` and `pdp8-colours.png`).
Its script holds the emulator, the two programs, the lamp sampling, the
layout arithmetic and the colour table. Ken's settings (WI 4783) are:

- colours by register on the LED palette;
- the walking-bit program;
- about 2 steps a second and 2 redraws a second;
- 16 brightness levels at 100 % max brightness;
- 89, 72 or 61 lamps, or nothing, depending on width.

## Premise check

- **The premise holds.** The mock-up and both PNGs were in place, and no
  PDP-8 or rotation code existed. `src/widgets/lamp_panel.c` was
  Altair-only, as 044 left it.
- kpidash is not in the cross-project-planning routing table.
- The preflight was clean: an empty tree, and local `main` level with
  `origin/main`.

## Decisions

- **The key is `kpidash:cmd:lamps:period`**, a plain decimal integer of
  seconds with no TTL. It follows the `KPIDASH_KEY_CMD_*` pattern in
  `protocol.h` and is read in redis.c's dev-command step beside
  `cmd:grid` and `cmd:textsize`.
  - It is a knob Ken sets and leaves, unlike those two, which expire after
    300 s, so it has no TTL.
  - Values: absent, or anything that is not a positive decimal integer,
    gives 900. Anything else is clamped to 10 .. 86400.
  - The cap exists so that `period × 1000` cannot overflow the 32-bit tick
    arithmetic. A day is far beyond any sensible rotation.
- **A period change applies at once, not at the next switch.** The rotation
  compares the time the current machine has been on with the period on
  every 500 ms tick. It never stores a deadline that a new period would have
  to recompute.
  - Shortening the period below what the machine has already had switches
    within half a second.
  - Lengthening it lets the machine stay on until the new period, counted
    from when the machine came on.
  - Why: it is the simplest rule with no state to invalidate, and Ken's
    "fast switch" then does what it says the moment he sets it.
- **The interface is four things** (`src/lampmach.h`): a machine's colours,
  a reset, `advance(ms, frac)` (run, then sample and reset), and
  `layout(free_w)`. The layout returns lamps (x, y, w, h, id, palette index),
  labels (text, x or centre, baseline, font, colour), a lamp radius and
  "drawable". Both machines live behind it in `lampmach.c`. The widget knows
  neither of them.
- **The rotation is a pure function on an injected clock**
  (`lm_rotation_tick`), so it is tested host-side.
  - A machine that does not fit is skipped. When it stops fitting it is
    replaced at once.
  - If only one machine fits, it stays on at the end of its period without
    restarting.
  - If none fits, the card hides.
- **A machine restarts from reset when it comes on screen, and only the
  machine on screen runs.** The Altair's first showing uses 044's seed, so
  its renders are unchanged. Each later showing seeds the player one higher,
  so the panel does not replay the same game every half hour.
- **The Altair starts first**, so the panel looks exactly as it did until
  the first switch.

## What shipped

- **`src/pdp8.{h,c}`**, pure:
  - The mock-up's PDP-8: the eight basic instructions with FETCH / DEFER /
    EXECUTE, operate groups 1 and 3, the group 2 skips and CLA, auto-index
    (0010-0017) and CDF.
  - The two programs, `walk` (the default) and `count`, selected by
    `PDP8_PROG`.
  - Lamp sampling once per machine cycle across 89 lamps.
  - The layout: three modes plus none, with the mock-up's thresholds.
  - The per-register colour table.
  - Every tuning constant at the top of `pdp8.h`.
- **`src/lampmach.{h,c}`**, pure:
  - The interface, with the Altair and PDP-8 adapters.
  - The shared palette mix (`lm_palette`) and level function (`lm_level`).
  - The rotation.
- **`src/widgets/lamp_panel.c`**, rewritten against the interface:
  - It draws whatever machine the rotation puts on screen.
  - It keeps one pre-mixed 16-level palette per colour per machine, built
    once at create.
  - It repaints only lamps whose level changed.
  - The 500 ms timer and 0.6 s catch-up cap are unchanged.
  - New calls: `lamp_panel_set_period` (`ui_refresh` passes the parsed key on
    every poll; 0, a dev-command state that never saw a poll, reads as the
    default) and `lamp_panel_show` (for the render harness).
- **`src/redis.c`**: `redis_parse_lamp_period`, and the GET in the
  dev-command step. **`src/ui.c`**: the panel's y uses `LAMP_PANEL_H`, and
  the period is passed each poll.
- **Docs**:
  - `docs/CLIENT-PROTOCOL.md` §9.7 documents the key, how to set it and when
    a change applies.
  - `docs/ARCHITECTURE.md` covers the PDP-8, the interface and the rotation.
  - The README layout sketch, and the gate source lists in the `justfile`
    and `CLAUDE.md`, are updated.
- **Tests**:
  - `test_pdp8` covers:
    - every memory-reference instruction and its major states;
    - DEFER, auto-index, and a non-auto-index location;
    - groups 1, 2 and 3, CDF, and reset;
    - both programs over 100,000 cycles against the mock-up's own emulator,
      on every lamp's on-count and the registers;
    - the walking bit over 40 steps: one bit, moving one place per step
      through the 13 bits of link and AC, while MQ counts;
    - lamp fractions over a redraw, and the step rate: 60 s keeps the clock
      to within one cycle, and the walk makes about 2 steps a second;
    - the colours;
    - the layout at 8, 12, 13, 14, 15 and 16 cards, lamp by lamp and label
      by label, against the mock-up's `layout()`;
    - the thresholds at their exact edges, and a sweep of every width from 0
      to 3792: no duplicate lamp, nothing outside the card, and the mode
      never falls as the width grows.
  - `test_lampmach` covers:
    - the Altair adapter against `alt_layout` at every width, plus spot
      checks of its labels against 044's `build()`;
    - `lm_palette` against `alt_palette`, and the PDP-8's four palettes
      against the mock-up's `mix()`;
    - the rotation on an injected clock: the default period, a changed
      period (both shorter and longer), the clamp, skipping, none fitting,
      and tick wraparound.
  - `test_redis_json` covers the key's parser: absent, valid, junk, sign,
    clamped up and clamped down.
  - The expected values in `tests/fixtures/pdp8/expected.h` are generated by
    `tests/fixtures/pdp8/gen.js`, which runs the mock-up's own script under
    node with a stub DOM.
- **Render harness**: `render_lamp_panel` takes an optional machine
  argument. ctest renders the PDP-8 at 8, 12, 13, 14, 15 and 16 cards, with
  the same gap and edge checks as the Altair.
  `sprints/planning/pdp8-widths-lvgl.png` is its output, for comparison with
  `pdp8-widths.png`.

## Where the port differs from the mock-up

- **The name label "PDP-8/I"** uses bold 24 where the mock-up asks for
  700-weight 22 px. 044 mapped the Altair's name the same way. Every other
  label is bold 20, which covers both the mock-up's 600 and 700 weights at
  20 px. The name fits in every mode that shows it (from a 21.7 px pitch).
- **Strip geometry**: none. The mock-up already used the real strip found in
  044 (cards from x 24 on a 228 px pitch, strip ends at x 3816).
- **The time base** is an LVGL timer with the elapsed `lv_tick` instead of
  `requestAnimationFrame`, under the same 0.6 s cap, as in 044.
- **Ported unchanged, though it may look odd, because the mock-up is the
  spec:**
  - The step counter, PAUSE and ION are never lit, because nothing in either
    program drives them.
  - DEFER is dark while `walk` runs, because the program uses no indirect
    addressing.

## Correction after the overseer's review: the state lamps

The first cut (`bbffae5`) ported a fault in the mock-up. The mock-up sampled
the major-state lamps after the cycle had advanced the state. So a FETCH
that decodes an ISZ lit EXEC, and in the delay loop EXEC read about 2/3 and
FETCH about 1/3.

The overseer ruled that a lamp shows the state of the cycle that just ran.
They corrected the mock-up at `kai:/scratch/kpidash-pdp8/`, changing nothing
else in it: `sample(st)` now takes the state the cycle was in (0 fetch,
2 defer, 1 execute).

- The corrected mock-up was copied over `sprints/planning/pdp8-mockups.html`.
- `pdp8.c` makes the same change: `sample(m, st)`, with PDP8_FETCH, _DEFER
  or _EXEC passed by each branch of `pdp8_cycle`.
- `tests/fixtures/pdp8/expected.h` was regenerated from the corrected
  script. Only the six state-lamp counts per program changed.
- `test_pdp8` now expects the following in the delay loop:
  - FETCH about 2/3, EXEC about 1/3 and DEFER 0;
  - on an indirect TAD, each of FETCH, DEFER and EXEC lit exactly once.
- `pdp8-widths-lvgl.png` was re-rendered. FETCH is now the brighter of the
  two lamps, and nothing else in it changed. The Altair renders are still
  byte-identical to 044's.

## The Altair code path

- **`src/altair.{h,c}`: no change.**
- **`lamp_panel.c`** no longer calls `alt_*` itself; `lampmach.c`'s adapter
  does. The adapter builds the same lamps and the same labels, in the same
  positions and fonts, that 044's `build()` created.
- **One change in what is created:** the widget now creates all lamps and
  then all labels, where 044 created each per-lamp label right after its
  lamp. Nothing overlaps, so the order makes no visible difference.
  - **Proof:** the five Altair renders (8, 12, 14, 15, 16 cards) are
    byte-identical to renders of `main` taken before any change (`cmp`).
- **When it resets:** the Altair now resets each time it comes on screen,
  and later showings get a new seed. Before, it reset once at startup.

## Gates

- `just check`: all checks passed. The dashboard ctest passes 23/23,
  `test_pdp8` and `test_lampmach` included. `check-release` builds `pdp8.c`
  and `lampmach.c` at `-O3 -Werror`.
- `just check-warnings-full` builds the whole tree, `lamp_panel.c` and
  `ui.c` included, at `-O3 -Werror` with no warnings. Its ctest passes
  40/40, including the six `render_lamp_panel_pdp8_*` tests.
- Both gates were run again after the state-lamp correction, with the same
  results: 23/23, then 40/40 with zero warnings.

## Pre-ship reading

Before the deploy, the panel used **11.6 % of a core**. Method: 120 s of
`utime + stime` from `/proc` at 100 Hz, ending 12:32 PST on 2026-11-16, on
`kpidash.service` pid 3755.

- The build was `799b1ee`, with the Altair on screen (the only machine it
  has).
- It was read on rpi53 over ssh from kai, the deploy host, with no second
  process started.
- 044 read 9.5 % on the same build right after its deploy. These single
  readings move by a couple of points from run to run.

The after reading has to be taken with the PDP-8 on screen. It belongs to
the deploy, below. The overseer's ruling on method:

1. Set the period key to 300 s, so the 120 s window sits inside one PDP-8
   showing.
2. Use 60 s for the two `krpidss` captures.
3. Delete the key.
