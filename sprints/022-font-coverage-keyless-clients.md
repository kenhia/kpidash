# Sprint 022 — the panel font covers what agents type, and a host earns its card by publishing

Proposal korg:2985 (program korg:2981, "Low-hanging fruit — experiment 1"),
covering WI 2646, 2524 and 2306. Leg `kpidash-0f0e84` on kai, headless, under
`/overseen-sprint`.

Three items that had each been parked on a decision somebody had to make
rather than on work somebody had to do. The program's ruling — recorded in the
proposal's `notes` — settled all three, so this sprint is the execution.

## Premise check

| item | claim | verdict |
|---|---|---|
| 2646 | fonts built from `0x20-0x7E,0xB0` only, so `·` U+00B7 has no glyph | **holds** — `RANGE` in `fonts/generate.sh`, unchanged since sprint 002 |
| 2646 | the warning is emitted per redraw, with no dedupe | **holds** — `lv_draw_label.c:393`, and `lv_conf.h` had `LV_LOG_PRINTF 1`, which prints straight to stdout |
| 2646 | rpi53's journal carries the flood | **drifted, same direction** — **zero** glyph warnings in the last 24 h, because kmon moved its card text to an ASCII separator exactly as WI 2646 recorded. No live card carries a non-ASCII byte today. The mechanism is untouched: the next publisher to type an em dash reproduces it |
| 2524 | `kwork` is in `kpidash:clients` with no client keys | **holds** — re-measured live on rpi53 today: six members, five with live health TTLs, `kwork` with `TTL -2` and no `kpidash:client:kwork:*` key at all. Its only key anywhere is `kpidash:cmd:graph:kwork` |
| 2524 | nothing ever removes a member | **holds** — no `SREM` in the repo |
| 2306 | service discovery is `SCAN`-based, so a never-published service has no card | **holds** — `redis_poll_services()`, and §8a has no declared-services concept |

## Decisions

### 2306: accepted and narrowed, with the reason written down

The overseer's call, and the cheap one of the three options WI 2306 listed. A
declared-services key would bind both consumers (kpidash and kdeskdash) and
buys the case that has never bitten — a service that has never published has
never been trusted, and the stale-GREEN failure that commissioned WI 902 is
fixed. The narrowing is now **in the contract** (`docs/CLIENT-PROTOCOL.md`
§8a) rather than in a korg comment, stated as a scope boundary with its
reason, so the next reader meets the decision and not the gap.

### 2524: admission-on-data, and the second half is the important one

The obvious reading of "a member with no client keys renders no card" is to
check the keys every cycle and skip the member when they are absent. That is
wrong, and measurably so: **every** client key is TTL'd — health 5 s,
telemetry 15 s, `dev_telemetry` ~5 s — so "no keys this cycle" is any host
more than fifteen seconds dead. Implemented literally, every real outage would
turn into a host that silently vanishes from the panel, which is precisely the
failure WI 902 exists to prevent.

So the rule is **admission**, not filtering. `registry_admit()` creates a slot
the first time a member actually publishes, and a member that has been
admitted **keeps** its slot when the data stops. `kwork` never publishes and
so never appears; a dead kai keeps its card and goes red.

No `SREM`, deliberately — a `kwork` that starts publishing tomorrow appears by
itself, with no operator step. That was the program's instruction and it is
also the right answer: the set is the registration, the keys are the evidence.

The residual cost is stated, not hidden: "has published" lives in the
dashboard process, so a host that is **down across a dashboard restart** shows
no card until it publishes again. Closing that needs durable state the
protocol does not have, which is a contract decision — filed as korg #3012 and
documented in §1.

### 2646: widen the font *and* dedupe the warning — they fix different things

The two halves are not alternatives. Widening the range stops the box;
deduping the warning stops the flood. A character outside any finite range
will always exist, so without the dedupe the next unlucky codepoint just
restarts the 3,844-lines-in-two-hours behaviour with a different number.

`LV_LOG_PRINTF` is now `0`. That is not incidental: `lv_log_add()` runs its
`printf` path **and** any registered callback, so leaving it at 1 would print
every line twice and dedupe neither. `src/main.c` registers the print
callback — after `lv_init()`, never before, because `LV_GLOBAL_INIT()` resets
the struct the callback pointer lives in.

The range went from 96 codepoints to 199: ASCII, all of Latin-1 Supplement
(which subsumes both `°` from WI 363 and `·` from WI 2646), en/em dash, curly
single and double quotes, bullet and ellipsis. Chosen as "what a human or an
agent types without thinking of it as a special character", not as a guess at
Unicode.

## What shipped

- `fonts/generate.sh` — `RANGE` widened to
  `0x20-0x7E,0xA0-0xFF,0x2013-0x2014,0x2018-0x2019,0x201C-0x201D,0x2022,0x2026`,
  with each block's reason inline. All seven Montserrat Bold faces
  regenerated. `lv_font_icons_56.c` is byte-identical, which is a useful
  check on the generator's determinism.
- `src/logfilter.{c,h}` — per-codepoint dedupe of the missing-glyph warning.
  Pure: it decides, it does not print, which is what makes it reachable from
  a gate with no LVGL. Bounded at 64 distinct codepoints; past the bound it
  suppresses rather than emits, because emitting is the flood.
- `src/main.c` — `lv_log_print_cb()`, registered after `lv_init()`. Same
  destination and same `fflush` as the path it replaces; the first sighting
  of a codepoint prints one extra line saying the repeats are being dropped,
  so a reader is not misled into thinking the character was drawn once.
- `lv_conf.h` — `LV_LOG_PRINTF 0`, with the reason.
- `src/registry.{c,h}` — `registry_find()` (never allocates) and
  `registry_admit()` (the WI 2524 rule). `registry_find_or_create()` now
  routes its search through `registry_find()` and rejects a NULL/empty
  hostname instead of walking off it.
- `src/redis.c` — `redis_poll()` reads health and telemetry **before**
  deciding whether the member gets a card, and calls `registry_admit()`.
- `docs/CLIENT-PROTOCOL.md` — three contract statements: §1 host-card
  admission, §8a the safe character set for `text`, §8a the WI 902 narrowing.
- `tests/test_logfilter.c`, `tests/test_client_registry.c`,
  `tests/shell/test_font_coverage.sh`, and `just check-fonts` in the gate.

## The gate grew a third leg, and it needed one

`fonts/*.c` are generated artifacts committed so a cross-compile needs no
Node.js. That is the right trade and it buys one specific way to be wrong:
widen `RANGE`, update the docs to match, never re-run the generator — and
nothing fails. The docs then promise a character set the panel does not have,
and the only symptom is a box on a screen in another room plus a log line
nobody is reading.

`just check-fonts` asserts the two ends against each other: it reads `RANGE`
from `generate.sh` (rather than restating the list, which would be one more
thing to drift) and checks every codepoint against the cmaps actually
compiled into each committed font. It needs no Node.js itself.

**Both failure directions were driven to exit 1** before the gate was
believed: `RANGE` widened with an uncovered codepoint → 7 failures naming
`U+4E2D` and the command to fix it; a font file reverted to its ASCII-only
version → failures naming `U+00A0` onward.

That exercise paid for itself immediately. The first version of the gate
**passed** the deliberately-broken case: `printf '%s'` without a trailing
newline leaves `read` with an unterminated last line, so it returns non-zero
and the loop exits *without running the body* — silently dropping the final
entry of `RANGE`. The gate was reporting 198 required codepoints instead of
199 and never checking `…` at all. A gate nobody has watched fail is a
decoration.

## Verification

- `just check` — 11 tests, all pass (two new), plus ruff, 80 client tests,
  `unit-lint`, the CD-19 shell gate and the new font gate.
- `just check-warnings-full` — whole tree at `-O2 -Werror`, clean. It matters
  here because `main.c` is dashboard-only and `check-release` cannot reach it.
- **Pi cross-build**, `-DCMAKE_BUILD_TYPE=Release` against the synced
  sysroot: clean, **including the link**. CLAUDE.md names aarch64-only linker
  warnings as the class neither gate sees; this sprint changed `lv_conf.h`,
  added a translation unit and replaced every font object, so it was read
  rather than assumed.
- **Binary cost of the wider font**, measured by rebuilding the same tree
  with the old font objects: 1,783,640 → 1,980,264 bytes, **+196,624
  (+11.0%)** on the Pi binary. Paid once, in flash, on a machine with plenty.

Live verification on rpi53 — a card carrying an em dash renders it, the
journal stays quiet, and no `kwork` card — belongs to the deploy, which this
repo does from merged `main`. See `## Deployed`.

## Repaired in passing

- **`registry_find_or_create()` walked a NULL hostname.** `strncmp` against a
  NULL `hostname` was reachable from any `SMEMBERS` reply element with a NULL
  `str`. Now rejected, and covered by `test_client_registry`.
- **The font-coverage gate's own dropped-last-entry bug**, above. Found by
  breaking the gate on purpose; it would otherwise have shipped silently
  under-checking.

## Filed, with the decision named

- **korg #3012** — a host down across a dashboard restart shows no card,
  because admission-on-data has no durable "has published" marker. Four
  options, each of which mints something (a new key in a two-consumer
  namespace, a client-protocol change, local dashboard state, or an explicit
  acceptance). Not a call this sprint had standing to make.

## Left alone, deliberately

- **No `SREM` of `kwork`.** The program's instruction, and correct: the fix
  is that membership stops meaning "render me", not that the membership is
  wrong.
- **`fonts/generate.sh`'s `npx lv_font_conv`** is unchanged. It failed on
  first use here (`npx canceled due to missing packages and no YES option`)
  and then worked unchanged once the package was in npx's cache. The script
  documents `npm install lv_font_conv` as a prerequisite and behaves as
  documented; adding `--yes` would make it install from the network silently,
  which is a different promise than the one it makes.
- **`kpidash-cards` is still not installed on kai** (noted on WI 2646 during
  program korg:2232). Nothing in this repo installs the shell helpers; whether
  `install.sh` should own `~/.local/bin` for them is a separate call.
