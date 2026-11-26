# Roadmap

> The general plan for this project. Keep it current; detail lives in the
> sprint records. Work items live in `korg` (project `kpidash`) — this file is
> the shape, not the queue.

## Now

- **050 — kpidash-client 2.1.1: `nvidia-ml-py` replaces `pynvml`** (korg
  proposal 5076, WI 4608). Same `import pynvml`, without the deprecation
  notice the shim printed on every client start. Patch release + fleet rollout.
- **049** (shipped). Usage card with Codex; Codex marker on Agents bars (korg:4950).
- **048** (shipped). Service card draws multi-line cards (`title`, `lines`,
  `span`); client 2.1.0 (korg:4853).
- **047** (shipped). Row 2: narrower Agents card, apt-temps widget beside it (korg:4852).
- **046** (shipped). Claude limits card: split-circle gauges (korg:4802).
- **045** (shipped). PDP-8/I lamp panel and rotation between panels (korg:4784).
- **044** (shipped). Altair 8800 lamp panel after the status cards (korg:4778).
- **043** (shipped). **Row 2 order: Claude usage first** (korg proposal 4756, WI 4752).
  Ken's request: row 1 in code (his "row 2") now reads Claude limits
  (column 0), 10G (1-2), NAS (3-4), korg + klams (5). Positions only.
- **042** (shipped). Agents card v2 (korg proposal 4734, slice 7 of program 4699). The
  first live day showed 10-30 minute sessions as 25 px bars with no room for
  a name, so the card now shows the last 3 hours in detail (names inside the
  bar or just left of it) over a thin 24-hour strip, one lane per host. View B
  of `planning/agents-mockups-v2.html`, at 3 hours.
- **041** (shipped). Agents card, row 2 columns 0-3: agent sessions as
  host-bordered bars, legs hung under their overseer, ships as diamonds, from
  `kdash:agents` (kdashdata CD-31, written by ksitrep). The "Border colour" +
  links "On" view of `planning/agents-mockups.html`.
- **040** (shipped). Time and calendar card, row 2 columns 4-5, from
  `kdash:calendar` (`planning/calendar-mockups.html`, card A).
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
  is the calendar card (040, program korg:4669), and columns 0-3 the Agents
  card (041, program korg:4699). Every cell of the grid is now spoken for.
  The parked alternative for row 2 is the Airspace card
  (`planning/row3-mockups.html`, korg WI 4692).
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
