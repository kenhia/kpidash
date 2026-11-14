# Roadmap

> The general plan for this project. Keep it current; detail lives in the
> sprint records. Work items live in `korg` (project `kpidash`) — this file is
> the shape, not the queue.

## Now

- **040 — time and calendar card** (korg proposal 4668, slice 3 of program
  4669). A 2×1 at the right end of row 2 (columns 4-5): a 24-hour clock, the
  date, the next tagged days, and the month marking weekends, Ken's days off,
  holidays and special days from `kdash:calendar` (kdashdata CD-30, written
  by korg). Card A of `planning/calendar-mockups.html`.
- **036-039** (shipped). 036 retired the client's unread producers
  (kpidash-client 2.0.0). 037 and 039 built the Claude limits card in row 1,
  column 5; 038 the korg + klams card in column 4
  (`planning/row2-cards-mockups.html`, program korg:4619).
- **035** (shipped). Host column hold and NO PATH debounce: two timing rules
  that stopped the host row and the 10G card flickering.
- **034** (shipped). Weather filler in the host row's spare columns,
  "C · Range" (`planning/weather-mockups.html`, vC).
- **033** (shipped). The NAS card rebuilt to "A · Rail" in mauve
  (`planning/nas-card-mockups.html`, vA).
- **030-032** (shipped). 030 widened the host columns across all of row 0
  (`planning/hosts-wide-mockups.html`); 031 gave them short OS names; 032
  removed the dev graphs, Fortune, Activities and Repo Status and fixed the
  10G and NAS cards in row 1. 029 built the host columns
  (`planning/hosts-columns-mockups.html`, first brainstorm
  `planning/refresh-mockups.html`).

## Next

- **What fills the free cells.** Row 1, columns 4-5, came from program
  korg:4619: Claude limits (037) and korg + klams (038). Row 2, columns 4-5,
  is the calendar card (040, program korg:4669). Row 2, columns 0-3, is still
  open, and ideas for it are still owed.
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
