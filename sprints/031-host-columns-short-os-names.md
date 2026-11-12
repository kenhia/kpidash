# 031 — Host columns: short OS names instead of an ellipsis

Proposal korg:4558. It covers no work items by design, so the brief is the
proposal's notes. It came from the overseer's final look at korg:4545
(sprint 030). Ken's answer was "Yes, shorten them".

## Goal

Sprint 030's full-row host columns put the OS name, muted and right-aligned,
on the uptime line, and real names clipped with the ellipsis: "Ubuntu
24.04.5 L..", "Debian GNU/Linux 1..". Ken asked for name plus version, like
"Ubuntu 24.04".

## Decisions

- **Display side only, in the pure model.** `hostcols_short_os()` lives in
  `src/hostcols.c`, so it is unit-tested without LVGL, and `hostcols_build()`
  calls it to fill `hc_host_t.os`. `os_name` stays whole in Redis and in
  `client_info_t`, so no producer or contract changes (the brief said so
  explicitly).
- **The rule.** Keep the OS or distro name, then the first token that starts
  with a digit, cut to two numeric parts (`24.04.5` → `24.04`,
  `5.15.0-173-generic` → `5.15`). Stop after it. Drop the `GNU/Linux` token
  and any parenthesised span wherever it occurs.
- **Windows is the one special case.** In "Win 11 Pro", the "11" is part of
  the product name, so the general rule would lose the edition. A string
  whose first word is `Win` or `Windows` keeps words until the first feature
  update (`26H2`) or build number (a word made of digits and dots, with a dot
  or at least four digits), and drops the rest.
- **Pass-through.** A name with no version ("Arch Linux", "Omarchy") is kept
  whole. If nothing survives, as with `(weird)`, the input is returned
  unchanged. `text_at()`'s clip-with-ellipsis stays as the last resort.
- Compact tiles draw no OS name and are untouched.

## Fixtures

The real `os_name` of every reporting host, read from rpi53's Redis on
2026-11-12 (`kpidash:client:*:health`):

| host | published | shown |
|---|---|---|
| kai, kubs0, kubsdb | Ubuntu 24.04.5 LTS | Ubuntu 24.04 |
| gratch | Ubuntu 26.04.1 LTS | Ubuntu 26.04 |
| rpi53 | Debian GNU/Linux 13 (trixie) | Debian 13 |
| cleo | Win 11 Pro 26H2 26300.9818 | Win 11 Pro |
| kimac | macOS 27.0.1 | macOS 27.0 |

`test_hostcols` covers these, the client's own fallbacks (`Windows 11`,
`Linux <kernel>`), a few neighbouring shapes, the pass-through cases, a short
output buffer, and `hostcols_build()` using the function while leaving
`os_name` alone.

## What shipped

- `src/hostcols.{c,h}`: `hostcols_short_os()`, used by `hostcols_build()`.
- `tests/test_hostcols.c`: `test_short_os`.
- `just check` is green (test_hostcols 159 passed), and so is
  `just check-warnings-full`.
- Offline render (`render_host_columns -f`) of the seven real strings, with
  every uptime set to a deliberately wide `up 189d 0h`: all seven columns show
  the short name with no ellipsis (`.scratch/031-render-top.png`).

## Deployed

Deployed 2026-11-12 at 12:03 PST, from kai, on the ship turn. That followed
the overseer's clearance (korg:4558 comment 4605) and the squash merge
`df59d7c` (PR #34).

- **rpi53**: `scripts/deploy.sh` ran from merged `main`.
  - The service is active with `NRestarts=0` and reports `df59d7c
    (2026-11-12)`.
  - The aarch64 build and link printed no warnings
    (`.scratch/deploy-031-ship.log`, grepped).
  - `~/kpidash` matches `build-pi/kpidash` (md5 `4e9216c0…`).
- **Live capture** (`scripts/krpidss`, `.scratch/live-031-ship.png`): all
  seven full columns show the short name with no ellipsis. kai, kubs0 and
  kubsdb show "Ubuntu 24.04", gratch "Ubuntu 26.04", cleo "Win 11 Pro", kimac
  "macOS 27.0" and rpi53 "Debian 13".
