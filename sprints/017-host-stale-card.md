# Sprint 017 — "\<host\> stale" card in the service row

- **korg**: work item #1903, proposal 1918. **Slice 5 of program 1919**
  (karc acceptance: host staleness — from the laptop's lid to the
  dashboard), the visible end. Depends on slice 2 (kdashdata registered
  the feed, CD-18), slice 3 (agent-skills writes its flag), slice 4
  (k-homelab writes its flag).
- **Branch**: `017-host-stale-card`
- **Run as**: a headless `karc` leg (`kpidash-1f8d25`) on kai, under
  `/overseen-sprint`. The overseer is a session on cleo; the ship is
  gated on its green light.
- **Scope**: the read side only. kpidash SCANs `kdash:stale:*` in its
  once-a-second poll and renders one service-row card per stale host.

## Narrative

**2026-09-05** — Slice 4 handed this one a genuinely stale komarchy
rather than a fixture, which is the whole reason the program cycled a
real laptop lid three times instead of seeding Redis by hand. First act
was to check that inheritance rather than assume it, with the two
controls slice 4 established:

```
no-auth PING : NOAUTH Authentication required.   # control A
auth PING    : PONG                              # control B

kdash:stale:komarchy:k-homelab
  {"reason":"audit skipped: unreachable","since":1788594159,"stale":true,"ts":1788594159}  ttl -1
kdash:stale:komarchy:agent-skills
  {"reason":"fleet-deploy skipped: unreachable","since":1788594170,"stale":true,"ts":1788594170}  ttl -1
```

Both standing, `since` 11 seconds apart with **k-homelab older**. That
gap is what makes #1903's "oldest `since` first" assertion falsifiable at
all — against a single key, or two written in the same instant, every
ordering looks correct.

Premise check on #1903, all holding: the 1s poll and its
`kpidash:services:*` SCAN exist as described (`redis_poll_services`,
called from `redis_poll`, driven by a 1s LVGL timer); the payload shape
matches the contract on the wire; the service card is 220×240 and
apt-temps already matches it, so "same size as the other service cards"
costs nothing. No drift.

### The one thing this reader must not get wrong

Every other SCAN-driven reader in this codebase drops an unparseable
record and moves on — `redis_poll_services` and `redis_poll_apttemps`
both do, and that is right when a record is the only thing asserting its
own existence.

Here the **key** asserts staleness and the payload only describes it. So
dropping an unreadable record does not lose a detail, it erases the
flag — and a host with no card reads as *healthy*. One parse bug would
report a three-week outage as fine. CD-18 writes this down; the overseer
carried it forward as the single most important thing for the reader to
get right.

Every failure path therefore falls back to *"still stale, detail
unknown"*, never to *clear*:

| What went wrong | What happens |
|---|---|
| Payload absent, empty, corrupt, not an object | Host raised; `since` dropped |
| `since` is an ISO-8601 string | Same — the record is rejected, the flag is not |
| `stale: false` published | Host raised; the writer left the key up, so it cleared nothing |
| Key missing/extra deployer segment | Host raised, deployer shown `(unknown)` |
| `SCAN` dies part-way | **Cycle aborted**; previous cards stay up |
| Key deleted between `SCAN` and `GET` | Skipped — the one correct skip |
| Key names no host (`kdash:stale:`) | Ignored; nothing to attribute |

The last one is the only shape this reader cannot honour, and it is
honest about why: there is no host to raise.

### Decisions

**The cycle protocol, and why `abort` exists.** The service and
apt-temps registries accumulate entries and only ever lose one to an
explicit evict command. This feed cannot work that way — absence is the
all-clear, so an entry not seen must go. That makes a *partial* scan
dangerous in a way it never was for the other two: committing one would
take cards down that nothing had cleared. So observations land in a
pending set and only replace the live set on `commit`; anything short of
reaching cursor 0 calls `abort` instead and leaves the cards standing.
A dead connection does not even open a cycle.

**The card lives in `service_card.c`, not a new widget class.** The
proposal asked for the existing widget and colour bands to be reused.
Reusing `service_entry_t` itself was the wrong way to do that —
`service_color()` would age the card to RED after 75 s, and this feed has
no freshness window at all. So the card is a second entry point in the
same module, sharing `SC_WIDTH`/`SC_HEIGHT`/`SC_RADIUS`, the border
thickness and `color_for()` literally rather than by a second set of
constants that drifts. `stale_card_update` takes no `now`, which is the
type system saying the colour never depends on the clock.

**The title wraps rather than elides.** `komarchy stale` at bold 24 is
close to the card's inner width. The panel has no input devices, so a
title elided to `komarchy sta...` can never be revealed by any means —
the repo's own standing rule. Wrapping to two lines is the only option
that keeps it legible.

**An off-contract key raises its host.** The `kdash-pub` wrappers police
namespace and token charset but *not* segment count — the overseer flagged
this to slices 3 and 4 as a writer-side hazard. It is reachable in
production, so the reader treats a missing or over-long deployer segment
as a lost *name*, not a lost flag. This goes one step past the literal
contract, which describes only well-formed keys; it is the same inversion
CD-18 applies to payloads, applied to key shape.

**Deployers with an unknown `since` sort last**, not first. They carry no
ordering information, so putting them at the top would have them claim to
be the oldest. Ties break by name so the body does not reshuffle between
polls.

**One SCAN, not one round trip.** The proposal said to keep the
`kdash:stale:*` scan "in the same pass rather than adding a second round
trip". `SCAN MATCH` takes a single pattern and the two prefixes differ
(`kpidash:services:` vs `kdash:stale:`), so literally one scan would mean
matching `*` across the whole 101-key keyspace and filtering client-side —
strictly more work. Read as *the same once-a-second cycle, no second
timer or thread*, which is what shipped: `redis_poll_stale()` sits beside
`redis_poll_services()` in `redis_poll`. Cost on the live feed is 1 SCAN
plus one GET per standing flag.

### On the test suite — planted, not trusted

This program has a running table of checks that went green for a reason
unrelated to the claim: slice 1's vacuous test, slice 3's loose grep,
slice 4's suppressed `NOAUTH` and its borrowed manifest. Every one was
found by planting the defect, never by writing more assertions.

Two things came out of taking that seriously here.

**A gap the first green run hid.** The suite asserted the *registry*
could hold an unreadable record — but `redis_poll_stale` needs a live
Redis, so nothing checked that the *reader* actually handed it one
instead of dropping it. The most important rule in the sprint was
untested at the level where the bug would live. Fixed by splitting the
per-key decision into `redis_stale_apply_record(key, payload)`, which
takes the payload as a string and is callable from the gate; the poll is
now the loop around it. Six unreadable payload shapes, including `NULL`
and the ISO trap, are asserted to raise the host.

**And my own live check went green for the wrong reason**, on the first
probe of the sprint: I parsed `kdash-pub endpoint`'s output as a URL when
it prints bare `host:port`, so the "no credentials must fail" control
failed with `Name or service not known` — a DNS error, not an auth error.
It looked exactly like the control passing. Re-run against the right
host, both controls behaved. Same family as the four above, and the
fourth instance of it in three days.

**Nine planted defects, all caught**:

| # | Defect | Result |
|---|---|---|
| 1 | Unreadable payload skipped (the CD-18 inversion) | caught (6 assertions) |
| 2 | Deployers sorted newest-`since` first | caught (9) |
| 3 | Aborted cycle commits the partial scan | caught (2) |
| 4 | `rpi53` no longer excluded | caught (3) |
| 5 | ISO-8601 string accepted as `since` | caught (3) |
| 6 | Off-contract key dropped instead of raising | caught (3) |
| 7 | Unknown-`since` deployers sort first | caught (1) |
| 8 | Duplicate SCAN observation listed twice | caught (1) |
| 9 | A cleared host's card never taken down | caught (15) |

## What shipped

- `src/protocol.h` — `KDASH_KEY_STALE_PATTERN` / `_PREFIX`,
  `STALE_EXCLUDED_HOST`, `STALE_DEPLOYER_UNKNOWN`.
- `src/registry.{h,c}` — the staleness registry: `stale_entry_t`, the
  begin/observe/commit/abort cycle, `snapshot`, `find`, `reap`, and the
  `stale_format_title` / `stale_format_body` renderers (in registry.c so
  the render strings are assertable without LVGL).
- `src/redis.{h,c}` — `redis_parse_stale_key`,
  `redis_parse_stale_payload`, `redis_stale_apply_record`,
  `redis_poll_stale`, called from `redis_poll`.
- `src/widgets/service_card.{h,c}` — `stale_card_create` / `_update`.
- `src/ui.c` — reap-then-paint into the footer strip.
- `tests/test_stale_card.c` — 122 assertions.
- `docs/CLIENT-PROTOCOL.md` — §8c, and a note on the one key family not
  under the `kpidash:` prefix.

## Follow-ups

- The strip paints stale cards **after** the service and apt-temps cards
  (creation order in the flex row), matching how apt-temps was added. An
  alert arguably belongs leftmost; deliberately not done, because moving
  each new card to index 0 reverses the host order and the row is one
  glance wide either way. Worth Ken's eye during the live check.
- `reason` is carried by both writers, is one human-facing line by
  contract, and is not rendered. If the card ever grows a detail view,
  that is what belongs in it.
