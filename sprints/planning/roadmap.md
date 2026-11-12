# Roadmap

> The general plan for this project. Keep it current; detail lives in the
> sprint records. Work items live in `korg` (project `kpidash`) — this file is
> the shape, not the queue.

## Now

- **030 — Host columns across the full row** (korg proposal 4545). The 029
  widget widened to all of row 0 (3792×628) with the text roughly doubled, CPU
  average and top core on one line, and one row for the fullest drive in place
  of the drive list. Spec: `planning/hosts-wide-mockups.html`.
- **029 — Host columns** (korg proposal 4527, shipped). One widget in row 0
  replaced the six host cards: a full column per host with sparklines, compact
  tiles past ten hosts, up to thirty, attention by border colour. Spec:
  `planning/hosts-columns-mockups.html`; first brainstorm:
  `planning/refresh-mockups.html`.

## Next

- **NAS card restyle** (next slice after sprint 032) — make the NAS card sit
  with the 10G card in row 1, from a range of mock-ups Ken picks from.
- **What fills the free cells** — row 1 columns 4-5 and all of row 2 have
  been empty since sprint 032 removed the dev graphs. A later brainstorm.
- **What fills the free host columns.** With seven hosts, three of row 0's ten
  columns are empty (1111×596) since 030. A later brainstorm with Ken; he wants
  something graphical there, not Fortune.
- **Service card: kmon status with staleness alerting** (korg #902, M) — the
  card should distinguish "kmon says everything is fine" from "kmon has not
  said anything in a while", which currently look the same.
- **klams "unreachable" flapping** (korg #658, S) — the klams service card
  intermittently reports unreachable while klams is up. Likely the poll cycle
  or the freshness window rather than klams itself.
- **klams 2×1 widget** (korg #826, M) — promote klams from a binary up/down
  card to a real widget showing corpus size, scanner state and recent activity.

## Later / Ideas

- **A gate that covers the Pi build.** `just check` only builds the
  `TESTS_ONLY` target; a cross-compile regression is invisible until deploy.
  Would need a sysroot available to the gate, so it is not free.
- **`clang-format` / `cppcheck` in the gate.** The retired Spec-Kit constitution
  claimed both as pre-commit checks, but neither is installed on kai, so neither
  is enforced. Installing them and wiring the format check in is the cheap half.
- **Widget leak coverage in the gate.** `tests/test_widget_leak.c` only builds
  when LVGL is available, so the gate never runs it — the one test that guards
  the bug class sprint 005 existed to fix.
