# 018 — health sweep: the 1.6-core burn, kmon staleness, harness re-apply, warning-clean, apttemps repoint

Proposal: `korg:2229` (slice 7 of program `korg:2233`, the sub-1000 backlog drain).
Run as an overseen karc leg (`kpidash-5cb0a9`) on kai, 2026-09-10.

Five items. One of them is the whole sprint and the other four are debts that
had been waiting for a trip to this repo.

## WI 1798 — the ~1.6-core burn (the real one)

**Measured before and after on rpi53, which is the only acceptance a
performance item gets.**

The diagnosis in the work item was written live with gdb and strace on
2026-09-01 and it held up completely against the tree:

- `lv_conf.h:24` was `LV_USE_OS LV_OS_PTHREAD`, and `LV_DRAW_SW_DRAW_UNIT_CNT`
  was never set — so it took LVGL's default of 1 (`lv_conf_internal.h:482`).
- `src/widgets/dev_graph.c:31` `POINT_COUNT 300`, nine series per chart,
  `lv_chart_refresh()` on every 1s poll (`:301`), three cards on screen.
  3 × 9 × 300 ≈ **8,100 `lv_draw_line` tasks a second**.
- The costly part is visible in the vendored LVGL: `lv_draw_sw.c:459` hands
  each task to the render thread with `lv_thread_sync_signal()` under
  `LV_USE_OS`, and takes the `#else` at `:463` — `execute_drawing_unit()`,
  inline — without it. On top of that `lv_draw_finalize_task_creation()` calls
  `lv_draw_dispatch()` after *every* task, walking the whole pending list each
  time, which is where the quadratic main-thread time went.

Only the item's file path had drifted (`dev_graph.c` is under `src/widgets/`).

**The fix is one line**: `LV_USE_OS LV_OS_NONE`. Nothing in `src/` uses LVGL's
threading primitives — the registries carry their own pthread mutexes and the
Redis poll is a synchronous step on the one LVGL thread — so there was no
LVGL-internal locking to lose.

| | before | after |
|---|---|---|
| `ps` %CPU (lifetime avg) | **109%** (over 5d04h) | **39.0%** |
| top, 120s-averaged | main 72–80% + render 10–39% | **39.4%**, single thread |
| threads | 2 | **1** — render thread gone |
| rpi53 load avg (1 min) | 2.02 | **0.52** |
| RES | 69,952K | 68,464K |

So **109% → 39% of one core, a 64% reduction**, and `Threads: 1 total` is the
structural confirmation that the handshake is gone rather than merely cheaper.

One honest note on the premise: the item said ~1.6 cores and the measured
baseline was 1.09. Drift downward, same direction, same root cause — a proceed
under the premise-check rule, not a re-scope. Option 2 in the item (fewer
points, dropping the flanking series) was **not** needed and was not done.

Measured by deploying this change **alone**, before the other four items, so
the numbers isolate it.

## WI 902 — kmon status with staleness alerting

**The premise had drifted materially, and saying so is most of the work.**

The item asks to "add a new special card type — Service Card". That card has
existed since sprint 006 (`src/widgets/service_card.c`, `service_entry_t`,
`kpidash:services:{name}:{host}`, polished in 013). Building a second one would
have forked a contract that now has two consumers — kpidash and kdeskdash 782,
which publishes to the shape kpidash already reads.

What was actually missing is much smaller and is the thing the card was
commissioned for: **a single global `SERVICE_FRESH_SECONDS` of 75.0**, against
which a *daily* feed is RED from about a minute after every successful run. So
"kmon is healthy" and "kmon has been dead for a week" render identically —
exactly the failure that prompted the item (kmon died silently for 10 days,
2026-07-23 → 2026-08-01, while the dashboard showed a stale GREEN "OK").

**Built against the documented shape, and it is not extended.** The contract in
`docs/CLIENT-PROTOCOL.md` §8a carries `ts`/`state`/`text`/`host`/`icon` and no
cadence field. Adding one would be inventing a card shape. The freshness window
is a **consumer-side rendering policy**, so it stays on the dashboard side:
`service_fresh_window(name)` returns 26h for `kmon` and 75s for everything
else, from a deliberately short table in `src/registry.c`.

26h and not 25h, per Ken's own note on the item: one hour for a DST shift, one
for timer jitter, on top of a ~24h refresh. kmon's timer is UTC-pinned so kmon
alone would be fine at 25h — the extra hour is for the next consumer, scheduled
in local wall-clock time.

**There is no live publisher.** kmon 901 (`korg:2076`) is unbuilt and sits in
program `korg:2232`, which is holding. This is built against a contract nothing
writes to yet — deliberately, on the overseer's instruction — and the required
handoff carrying the finalized contract is filed on WI 901.

Also corrected while in there: §8a documented the default window as `60.0 s`
when the code has used `75.0` since sprint 006. The code was right; the prose
was stale.

## WI 1279 — re-apply the harness with `--stack cmake`

`uvx --from git+https://github.com/kenhia/kprojects kproject-install --stack cmake .`

The gate came through **byte-identical** (md5 `d837879…` before and after), as
the item predicted — `_seed()` does not overwrite an existing justfile. The
managed block in `CLAUDE.md` and `.github/copilot-instructions.md` moved from
the `other` stanza to the cmake one; `.gitignore` gained `.sprint-defaults`,
`CMakeFiles/`, `CMakeCache.txt`, `compile_commands.json`.

Folded in the item's own suggestion: the gate now passes `--no-tests=error`.
Bare `ctest` prints "No tests were found!!!" and **exits 0**, so without it the
gate would pass loudest when there is least to check.

## WI 1934 — the warning-clean claim

Both warnings reproduced on the first Release cross-build, verbatim:

- `fortune.c:143` — ignored `fread` result (`-Wunused-result`). Now uses the
  count to terminate rather than discarding it.
- `ui.c:94` — `-Wstringop-truncation`. Sprint 017's `671ae10` fixed the same
  shape with an exact `memcpy`, but that worked because both sides were
  visibly `char[64]`. Here `add_card(const char *hostname)` loses the array
  bound, so an unconditional 64-byte `memcpy` would read past a shorter
  string. Used a bounded `strnlen`/`memcpy` with an explicit `memset` to
  preserve `strncpy`'s zero-fill of the tail.

**The durable half is making the claim testable, and it is only partly
possible here.** `just check-release` now compiles the same sources at
`-O2 -Werror` — and it passes — but TESTS_ONLY only compiles `config.c`,
`redis.c`, `registry.c`, `icons.c`, `memstat.c`. It would **not** have caught
either of these two, which live in dashboard-only sources. Closing that gap
needs a native full build, and **kai has no libpng-dev** (`pkg-config --exists
libpng` fails), so it cannot be done on this host without a machine change.

So the claim in `CLAUDE.md` is now precise rather than unqualified: gated at
`-O2` for the tested core, by inspection at deploy time for the rest. Filed as
a follow-up rather than installing a package unasked.

## WI 2244 — repoint apttemps onto libkdash

**The item's one open question resolved the expensive way.** It said to "check
whether this repo is already linking a version new enough to carry the
classifier and bump if not". kpidash was linking libkdash **not at all** —
`.gitmodules` had only `lib/lvgl`. So this was not a version bump; it was
adding a dependency to a cross-compiled C project with a TESTS_ONLY split.

Done following kstudiodash's pattern, with one difference: kpidash links
**`kdash_core` only** — the pure logic, no sockets, no hiredis — because it has
its own Redis layer and is not migrating it here.

**This changes a documented property**: `src/registry.c` calls
`kdash_apttemps_band()` and compiles into six test targets, so the tests-only
build needs the submodule too. `just check` therefore needs one submodule where
it needed none. Mitigated so the headline stays true: a private `_kdash` recipe
inits it, so a clean clone still passes in one command, and the property that
matters — no Pi, no sysroot, **no LVGL** — is unchanged.

**Acceptance was "nothing visible changes", so the test came first.**
`tests/test_apttemps_band.c` pins all three band edges on *both* sides (64.9
blue / 65.0 green, 75.0 green / 75.01 orange, 79.9 orange / 80.0 red) plus
staleness and the invalid/NULL cases — 17 assertions, and no apttemps test
existed before. They were written and run **green against the pre-repoint
arithmetic**, so their authority comes from the old implementation rather than
from the new one agreeing with itself. Still 17/17 after the repoint.

`APTTEMPS_COLOR_*` stays as kpidash's **palette** (CD-10: the library carries
no colours). Freshness also stays here — `kdash_apttemps_band()` takes `stale`
as a decision the caller already made, not a timestamp it judges.

This closes the kpidash half of the three-copies window that kdashdata sprint
009 opened. The sister item, kstudiodash WI 2245, is not covered by this sprint.

## WI 658 — not ours

Resolved by the klams slice (`korg:2220`) before this leg started: `/healthz`
now answers `Connection: close`, so the server tells every client not to pool.
No kpidash commit, no test, no client-side mitigation — writing one would add a
second timing constant to a race the server removed structurally. Observation
of the card after the klams 0.1.48 deploy is reported in the wrap-up.

## Gate

9 C tests (7 before; `test_apttemps_band` is new and `test_service_card` grew
from 45 to 66 assertions) + 80 Python tests, plus the new `-O2 -Werror` pass.

## Follow-ups

Filed as work items rather than done here — see the wrap-up handoff for numbers.

- The Release warning pass cannot reach the dashboard-only sources because kai
  has no `libpng-dev`. Needs a machine change, which is Ken's call.
- `ld` warns that `redis.c.o` "requires executable stack" on every link.
  **Confirmed pre-existing on `main`** via a throwaway worktree, so it is not
  from this sprint — but it is a warning in a repo that advertises
  warning-cleanliness, and it is a linker warning, which neither `-Wall` nor
  the new Release pass covers.
- A service that has **never** published is indistinguishable from one that
  does not exist: cards are created from discovered keys, so a missing key
  renders as no card rather than as RED. WI 902 asks for "not a silently
  skipped card", and closing that needs a declared-services list, which the
  documented contract does not have. Surfaced rather than resolved
  unilaterally.

## Deployed

`scripts/deploy.sh` → **rpi53**, 2026-09-10 23:41 PDT. Service active,
`NRestarts=0`, journal clean for the ten minutes after. Two deploys this
sprint on purpose: the first carried WI 1798 **alone** so its before/after
numbers isolate it, the second carried everything.

Verified live on the Pi, with the probe run **from kai** for the build/deploy
path and **on rpi53 itself** for every reachability and load reading — the two
are different facts and only the second one is about the panel:

- **WI 1798 held under the full sprint**: 38.9% (`ps` lifetime) / 39.4%
  (90s-averaged `top`) of one core, matching the isolated measurement, so the
  other four items cost nothing measurable. Single thread; the render thread
  stays gone.
- **The Release cross-build is warning-clean** (WI 1934) — the two warnings
  are absent from the build that actually reaches the Pi, which is the only
  place that claim could be checked.
- **WI 2244 changed nothing visible.** Live zones read bedroom 71.0 °F, living
  71.0 °F, kitchen 69.9 °F — all GREEN before and after. Worth stating plainly:
  all three sit mid-band, so the *live* check confirms no regression but cannot
  exercise a boundary. The edges are covered by `test_apttemps_band.c`, which
  is why that test pins both sides of all three rather than sampling midpoints.
- **WI 658 observation** (asked for by the overseer, not our work): klams
  0.1.48 is deployed and `kpidash:services:klams:_` read `state: ok`,
  `"v0.1.48 up 0h24m"`, steady across six consecutive poll cycles. **No
  "Unreachable" flicker.** The server-side `Connection: close` fix holds from
  the consumer side.
- **WI 902 is unverifiable live and that is expected** — kmon publishes
  nothing yet, so no `kpidash:services:kmon:*` key exists and no card renders.
  The window is covered by unit tests only.
