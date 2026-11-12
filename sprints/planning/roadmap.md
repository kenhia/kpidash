# Roadmap

> The general plan for this project. Keep it current; detail lives in the
> sprint records. Work items live in `korg` (project `kpidash`) — this file is
> the shape, not the queue.

## Now

- **033 — NAS card restyle** (korg proposal 4567). The NAS card rebuilt to
  "A · Rail" in mauve, the 10G card's skeleton in the NAS's own colour: a
  state headline, eight disk tiles in Ken's order with a use label above each,
  nothing under 20 px, the CPU/RAM chart dropped. Spec:
  `planning/nas-card-mockups.html` (vA).
- **030-032** (shipped). 030 widened the host columns across all of row 0
  (`planning/hosts-wide-mockups.html`); 031 gave them short OS names; 032
  removed the dev graphs, Fortune, Activities and Repo Status and fixed the
  10G and NAS cards in row 1. 029 built the host columns
  (`planning/hosts-columns-mockups.html`, first brainstorm
  `planning/refresh-mockups.html`).

## Next

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
