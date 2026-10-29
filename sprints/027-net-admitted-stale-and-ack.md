# Sprint 027: net-admitted hosts read STALE after a restart; a reviewed warning can be cleared

Proposal korg:4158, covering WI 4085 and WI 4143. This is a wave-2 slice of
low-hanging-fruit program korg:4162, and it ran after the kdashdata slice
(korg:4152) shipped the CD-27 `kdash:net:ack:{host}` addendum. The submodule
was already pinned there (`4837473`, re-vendored in `063fcc0`), so nothing
needed re-vendoring. It ran as karc leg `kpidash-2ffa0f` on kai, headless,
under `/overseen-sprint`. Comment 4119 on the proposal added a one-line doc
repair, carried over from the apt-temps slice.

## Premise check

| claim | verdict |
|---|---|
| 4085: a host whose producer died before a restart reads NO FEED, not STALE | **holds**: `net_observe` only moved `LIVE → STALE`, and `g_hosts` started all-NOFEED |
| 4085: the admitted file has no sections, and `admitted_save` writes only the host set | **holds** |
| 4143: (a) time clears yellow by construction | **holds**: yellow comes from `lfc_delta_24h`, `rx_errors_delta_24h` and `drops`, all windowed by the producer |
| 4143: nothing can clear an ack today; CD-27 addendum decides "on the panel" | **holds**: kdashdata `4837473`, `kdash-net-ack.schema.json` |
| 4119: apt-temps writes the evict key automatically | **holds**: apt-temps `6d13b1a`, `dashboard.py` `EVICT_KEY`, `EX 60` |

## Decisions

### WI 4085: the `[net]` section (Ken's design, comment 4020)

- **The file and its name stay the same.** The headerless top is section `""`,
  the host cards' set, so the file already on rpi53 loads unchanged. `[net]`
  starts the net set. A section this build does not recognise is skipped on
  load and not written back. `admitted_save()` now takes both sets and always
  writes both, with the host set first and headerless. An empty `[net]` is not
  written at all, so a panel with no net-admitted hosts writes exactly the old
  format.
- **A malformed header line is an invalid line, not a section break.**
  `[Bad Header` fails as a hostname and is skipped, and the section it sits
  in carries on. Only a well-formed `[name]` changes section.
- **The net-admitted set is separate from the columns.** It is bounded by
  `MAX_CLIENTS` and append-only. A host dropped from `KPIDASH_NET_HOSTS` keeps
  its line, so adding it back later starts STALE. `net_set_hosts()` resets the
  set, so `main.c` seeds after it. Seeding marks the matching columns STALE
  before the first poll.
- **The record and the state are separate.** `net_host_t.has_rec` says
  whether a record has been seen this run. An admitted host with no key is
  `NET_COL_STALE` with `has_rec = false`. Its subtitle is
  `STALE · no record yet`, the headline gives `no record since the panel
  restarted`, and the column's data fields draw like no feed (`—`), because
  there is nothing to draw. Column attention is CRIT, and the headline is
  WARN with stale's rank, exactly like any other stale column. Ken's comment
  said "yellow". The existing stale rendering (grey hatched column, yellow
  headline) is what "like any other stale column" means in the code, and I
  matched that rather than giving it a third look.
- **Both dirty flags are taken every poll.** `save_admitted_if_changed()`
  reads both the registry's flag and netstate's before testing either, so a
  short-circuit cannot leave one set and save it a poll late.

**One-time consequence of this deploy.** The first start finds no `[net]`
section, so every net column reads NO FEED for about one producer interval
(~5 s) until each host's next write admits it. This matches the sprint-022
note for the host cards, and it never recurs.

**Rollback note.** A pre-027 binary reading the new file treats `[net]` as an
invalid line and skips it, but it then reads the net hosts below it as
host-card admissions. That is harmless here. All five
(`kubs0,kubsdb,kai,gratch,cleo`) are already in rpi53's host-card set, which
was read from kai on 2026-10-29 and held `kubs0 cleo rpi53 kai kubsdb kimac
gratch`.

### WI 4143: clearing a reviewed warning

- **Applied on the panel, per the CD-27 addendum.** Each poll's single `MGET`
  now carries `2n` keys: the n records, then the n ack keys. The acks go to
  netstate first (`net_observe_ack`), and the record is stored with the ack
  already applied (`net_apply_ack`). That function overwrites
  `lfc_delta_24h` and `rx_errors_delta_24h` with
  `min(delta, counter − baseline)`, or with the live counter itself after a
  reset. Every consumer (attention, flapping, headline, the LFC/rx lines)
  therefore sees the effective numbers with no second code path.
- **`drops` are filtered by `ack.ts`.** The contract names the counter
  deltas, but a drop in the last hour also turns the column yellow. Without
  this, "acking turns it teal within one tick" would fail for up to an hour
  after a real drop. A drop after the ack still counts, which is the
  contract's "anything after the ack is yellow again". The drop tick on the
  sparkline goes with it.
- **An ack does not clear "on 1G backup".** That warning is a condition that
  is still true now, not a past event, and time does not clear it either. The
  acceptance criterion "red is never cleared" covers stale, no link and no
  path, which are all untouched. **Flagged for the overseer**: this is a
  reading of the contract, not a line in it.
- **An ack that fails the schema is no ack.** The contract's counterexamples
  are a missing `lfc`, an empty `by` and a negative counter. They are
  test-gated against the schema file at run time, the same as the record.
- **`kpidash-client net ack <host> [--by NAME]`** (1.4.0). It reads the host's
  live `kdash:net:{host}`, copies `lfc` and `rx_errors` (null stays null), and
  writes the ack with `EX 86400`. It **refuses when the host has no live
  record**: a stale host is red and the ack would clear nothing. `--by`
  defaults to the login name. `net` became a click group with
  `invoke_without_command`, so `kpidash-client net` still prints one record
  exactly as before.
- **No "ack'd" marker** (overseer call). The cleared warning is the visible
  effect. The marker needs Ken's eyes on rpi53 and is offered in the wrap-up,
  not filed.

## What shipped

- `src/admitted.{c,h}`: `admitted_load_section()`, a sectioned
  `admitted_save()`, and `section_header()`.
- `src/netstate.{c,h}`: the net-admitted set (`net_seed_admitted`,
  `net_is_admitted`, `net_admitted_snapshot`, `net_admitted_take_dirty`),
  `has_rec`, `net_ack_t`, `net_parse_ack_json`, `net_apply_ack` and
  `net_observe_ack`.
- `src/redis.c`: `redis_poll_net` MGETs the records and the ack keys in one
  round trip.
- `src/protocol.h`: `KDASH_KEY_NET_ACK_PREFIX`.
- `src/main.c`: seeds `[net]` at startup and saves both sets when either
  grows.
- `src/widgets/net_card.c`: a STALE column with no record draws its data
  fields as `—`.
- `clients/kpidash-client`: `RedisClient.ack_net`, the `net ack` command, and
  version 1.4.0.
- Tests: `test_admitted.c` gains a legacy file with no `[net]`, a sectioned
  round trip (byte-exact), unknown and malformed sections, and an empty
  `[net]` writing no header. `test_netstate.c` gains the ack schema's
  examples and counterexamples, admitted-no-key → STALE vs unadmitted →
  NOFEED, admit-once and dirty, ack clears and a new event returns, ack never
  clears red or 1G, and the ack arithmetic (reset, already-smaller delta,
  nulls). `test_redis_client.py` gains three `ack_net` cases.
- Docs: CLIENT-PROTOCOL §1 (file format), §8d (`[net]` and the ack section,
  including the documented plain `SET`), §9.6 (comment 4119), ARCHITECTURE,
  and the client README.

Gate: `just check` green, and `just check-warnings-full` green (the whole
tree at `-O2 -Werror`).

## Repaired in passing

- **CLIENT-PROTOCOL §9.6 named one writer of the evict key and called
  eviction "manual by design"** (comment 4119). apt-temps has written it
  automatically for `kind: apttemps` since its sprint 007. The table now names
  apt-temps as an automatic writer, the TTL column says `EX 60` by convention
  instead of `none`, and the paragraph records the shared-key race with
  `kpidash-cards` as noted, not guarded. This is a doc change; the gate does
  not check prose.

## Follow-ups

None filed. The optional "ack'd" marker is an offer in the wrap-up (overseer
call).

## Deployed

2026-10-28 19:25 PDT (02:25 UTC 10-29), from kai, to rpi53. This ran on the
ship turn after the overseer's clearance (korg:4158 comment 4150, which
accepted all three contract readings) and the squash merge `f5bd6fa` (PR #29).

- **`scripts/deploy.sh` from merged `main`.** Only `kpidash.service`
  restarted. It is active with `NRestarts=0`, and the dashboard reports
  version `f5bd6fa (2026-10-29)`. The rpidash card on the panel shows the
  same version. The aarch64 build and link printed no warnings
  (`.scratch/deploy.log`, grepped).
- **The `[net]` section.** Before the deploy, `/var/lib/kpidash/admitted`
  held the seven host-card lines and nothing else. The journal shows the new
  binary loaded `7 previously-admitted host(s)` and `0 previously
  net-admitted`. One second later it logged `admitted set now 7 host(s), 5
  net`. The file now ends `[net]` / `kubs0 kubsdb kai gratch cleo`. The
  one-time NO FEED window was therefore about 1 s, not 5 s: every producer
  had written within its 15 s TTL.
- **Live ack check, run from kai against rpi53 Redis.** Auth went through
  `kpidash-auth.sh`. Screenshots are `.scratch/live-027-*.png`.
  1. **Before**: kubsdb was still yellow from the real 2026-10-28 03:46 X550
     drop (`lfc 0`, `lfc_delta_24h 1` after a counter reset). The headline
     read `kubsdb LINK DROP` and the column showed `LFC 0 (+1)`.
  2. **`kpidash-client net ack kubsdb --by claude-kai`** (1.4.0 from the repo
     checkout) wrote `{"lfc":0,"rx_errors":0,"by":"claude-kai",…}` with TTL
     86392. On the next screenshot the kubsdb column was **teal** and the
     headline read `ALL 10G`.
  3. **Synthetic counter bump**: one `SET kdash:net:kubsdb` with `lfc 1`,
     `lfc_delta_24h 2`, written right after a real producer write. The column
     turned **yellow again** with `LFC 1 (+1)`, which is the effective delta
     `min(2, 1 − 0)`, and the headline went back to `kubsdb LINK DROP`. The
     producer's next write (≤5 s) restored the real record.
  4. **After**: teal and `ALL 10G` again. The ack stays in place. kubsdb's
     drop was investigated (klams 01a1836c), and this is exactly the clear
     WI 4143 says that session would have issued. It expires on its own at
     about 02:27 UTC on 10-30.
- **`just publish`**: kpidash-client **1.4.0** is in the package store
  (wheel and sdist). Fleet hosts do not need to upgrade, because the daemon's
  behaviour is unchanged. 1.4.0 is only needed wherever someone wants to run
  `net ack`.
