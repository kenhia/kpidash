# Roadmap

> The general plan for this project. Keep it current; detail lives in the
> sprint records. Work items live in `korg` (project `kpidash`) — this file is
> the shape, not the queue.

## Now

- **014 — kprojects harness** (korg #1263). Migrate off Spec-Kit: collapse the
  twelve spec directories into `sprints/`, drop the Spec-Kit machinery, give the
  repo its first real `just check` gate.

## Next

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
