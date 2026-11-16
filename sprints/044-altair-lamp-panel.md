# 044 — Altair 8800 lamp panel after the status cards

Proposal korg:4778 covers WI 4777. It ran as a karc leg (kpidash-a85d67) on
kai under an overseer. Branch `044-altair-lamp-panel`.

## Goal

Ken, 2026-11-16: fill the variable-width space right of the footer's status
cards with the front-panel lamps of an Altair 8800. A small in-process 8080
emulator runs Kill the Bit with a simulated player, and each lamp's
brightness is how long its line was high since the last redraw. It is
decoration, like the weather filler.

The specification is the animated mock-up Ken approved,
`sprints/planning/altair-mockups.html` (copied in from
`kai:/scratch/kpidash-altair/`). Its script holds the emulator, the cycle
model, the layout arithmetic and the colours. `altair-widths.png` beside it
shows four widths at real pixels. Ken's settings (WI 4777) are Kill the Bit
with the player, a 450 kHz clock (about 2 steps a second), 2 redraws a
second, 16 levels, 100 % max brightness, LED red `#FF4A3D`, and the Mocha card.

## Premise check

- **The premise holds.** The mock-up and PNG were in place, and no lamp or
  Altair code existed in the repo.
- kpidash is not in the cross-project-planning routing table.
- The start-sprint preflight found one untracked file at the repo root,
  `20261114-completed-dashboarda.png`. The leg paused on it, and the overseer
  ruled to leave it alone and stage named paths only. By the time work
  resumed, someone outside this session had moved it to `.scratch/`
  (ctime 11:10:18 PST, during the pause). This sprint never touched it.

## What shipped

- **`src/altair.{h,c}`**, pure (no LVGL, no clock):
  - The mock-up's 8080 subset: LXI H/B, MVI D, LDAX D, DAD B, JNC, JMP, IN,
    XRA D, RRC, MOV D,A, INX H and MOV A,M. Any other opcode jumps to 0, as
    the mock-up's does. Kill the Bit (24 bytes) and the address counter are
    built in. There is no third-party code and no ROM.
  - Per machine cycle, the T-states each of the 36 lamps was lit, with the
    mock-up's cycle split: fetch 4 (5 for MOV r,r and INX), memory read 3,
    DAD 6 internal.
  - `alt_run_ms` keeps the clock by carrying each run's overshoot.
    `alt_fractions` reads the lamps and resets them.
  - The simulated player (the mock-up's `senseSwitches`) runs on a seeded
    xorshift, so a run is reproducible. With the player off, IN reads
    `switches`, the front panel's sense switches, which is how the tests
    play.
  - The layout: free width in, mode (36 / 24 / none), pitch, lamp width,
    lamp x positions and label positions out. It uses the mock-up's
    thresholds and its octal grouping.
  - The 16-colour palette, mixed once with the mock-up's `mix()`.
  - Every tuning constant sits at the top of `altair.h`. Lowering the
    brightness is one line: `ALT_MAX_BRIGHTNESS`.
- **`src/widgets/lamp_panel.{h,c}`**:
  - An opaque Mocha card (radius 12, no border) holds one opaque rounded
    rectangle per lamp (62 px tall, radius 5). Labels are bold 20,
    per lamp or per group.
  - An LVGL timer every 500 ms runs the emulator for the time that passed
    (catch-up capped at 0.6 s, as the mock-up does). It sets a fill only on
    lamps whose level changed.
  - The widget lays out again only when its width changes. It hides below
    the thresholds, and nothing runs while it is hidden.
- **`src/ui.c`**: the panel sits at the footer's card height, centred in the
  strip as the cards are.
  - `ui_refresh` counts the strip's children. Only when the count changes
    does it force the flex layout and read the last card's right edge. The
    free span runs from there plus the strip's 8 px gap to the strip's right
    edge.
- **Tests**:
  - `test_altair` (in `just check`) covers the T-states of all 13 subset
    instructions, Kill the Bit walking the bit at 2 steps/s, and hit, miss
    and two-bit cases.
  - It checks exact lamp on-times over 100 delay-loop passes: M1 1/2, MEMR
    7/8, WO 1, the bit's lamp 1/4. It also checks the fractions over a whole
    redraw, and that 1000 runs keep the clock.
  - It checks that a seeded player is reproducible and both hits and
    misses, plus the counter, the levels, and the palette against the
    mock-up's `mix()`.
  - The layout is checked against the mock-up's `layout()`. The expected
    values came from running the mock-up's own script under node at its
    widths 1956, 1044, 588, 360 and 132. The thresholds are checked at their
    exact edges, and every lamp stays inside the card at every width from 0
    to 3792.
- **`tests/render_lamp_panel.c`** renders the footer at real size after N
  status cards. ctest runs it at 8, 12, 14, 15 and 16 cards, and it fails if
  anything is drawn in the gap before the panel or past the strip's edge.
  `sprints/planning/altair-widths-lvgl.png` is its output, cropped like the
  mock-up's PNG for comparison.

## Where the LVGL port differs from the mock-up

- **Status-card geometry, as found.** The cards are 220 × 240 on a 228 px
  pitch, as the mock-up assumed, but they start at x 24 (`PAD_LEFT`), not
  30. The strip ends at x 3816 (a 24 px right margin, not 30). Free widths
  at 8/12/14/15/16 cards are therefore 1968, 1056, 600, 372 and 144. The
  mock-up's were 1956, 1044, 588, 360 and 132. Every width lands in the same
  mode and label style.
- **Fonts.** The mock-up's 600-weight 20 px labels are the repo's bold 20.
  Its 700-weight 22 px "ALTAIR 8800" in per-lamp mode is bold 24, the
  nearest generated size at or above 20. The 700-weight 20 px names in group
  mode are bold 20.
- **The address bit is dim, as in the mock-up.** In Kill the Bit, D is on
  the high address byte only during LDAX's 3-T read, 12 of every 48
  T-states. So the lit bit sits at level 4 of 15, while A3 (the loop's PC)
  is near full. That is the mock-up's cycle model, ported unchanged, and it
  is what Ken saw when he approved it.
- **Time base.** The mock-up runs the emulator on `requestAnimationFrame`
  with the elapsed time. This runs it on an LVGL timer with the elapsed
  `lv_tick`, under the same 0.6 s cap.

## Gates

- `just check`: all checks passed. The dashboard ctest passes 21/21,
  `test_altair` included, and `check-release` builds `altair.c` at
  `-O3 -Werror`.
- `just check-warnings-full` builds the whole tree, `lamp_panel.c` and
  `ui.c` included, at `-O3 -Werror` with zero warnings. Its ctest passes
  32/32, the five `render_lamp_panel_*` tests included.

## Pre-ship reading

Panel CPU before the ship was **8.4 % of a core**: `kpidash.service` pid 856,
1008 ticks of `utime + stime` at 100 Hz over 120 s, ending 11:18 PST on
2026-11-16. It was read from `/proc` on rpi53 over ssh from kai, the deploy
host, with no second process started.

On kai at `-O3`, the emulator runs 60 emulated seconds in about 30 ms. The
Pi's share is 450 k T-states each second, plus at most 36 small opaque
repaints twice a second.
