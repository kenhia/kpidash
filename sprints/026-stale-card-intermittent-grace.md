# Sprint 026: an intermittent host's stale card waits out a week

Proposal korg:4114, covering WI 4111. This is slice 8b of program korg:4110,
kpidash's half of k-homelab **korg:4023 decision 2(i)** (Ken, 2026-10-23,
comment 3947). It ran as karc leg `kpidash-c8e2a4` on kai, headless, under
`/overseen-sprint`. Every probe ran **from kai**.

Before this sprint, a deployer that skipped an asleep host set
`kdash:stale:<host>:<deployer>`, and kpidash showed a yellow `<host> stale` card
as soon as the key existed. For a laptop that is just closed, that card is a
false call for attention. Decision 2(i) fixes it in one consumer and leaves
all three writers alone.

## Premise check

| claim | verdict |
|---|---|
| a card shows as soon as the key exists | **holds**: `stale_registry_commit_cycle` promotes every observed host, and nothing gates on age |
| the flag value might already carry availability | **it does not**: the payload is `stale`/`since`/`reason`/`ts` (CLIENT-PROTOCOL §8c, kdashdata `kdash-stale.schema.json`) |
| intermittent = komarchy, kbook2, kimac, thrall | **holds**: these are the four `availability: intermittent` entries in `kubs0:k-homelab/inventory.yml`, read through kaed |
| 7 days = `KMON_ABSENCE_DAYS` | **holds**: `DEFAULT_ABSENCE_DAYS = 7.0` in `kmon/satellite.py` |

## Decisions

- **Where intermittent comes from: a kpidash config list.** The variable is
  `KPIDASH_INTERMITTENT_HOSTS`, defaulting to `komarchy,kbook2,kimac,thrall`.
  The flag carries no availability, so the only answers that avoid a writer
  change are this list and the health payload's `availability` bit
  (WI #3132, `client_info_t.intermittent`). I rejected the health bit. It
  lives only in memory and is learned from a health write, so a laptop that
  is asleep when the dashboard restarts would be "always-on" until it wakes.
  It would also tie this feed to whether the host runs kpidash-client at all.
  The list is a second copy of an inventory fact, and the comment on
  `INTERMITTENT_HOSTS_DEFAULT` says where the source of truth is.
- **The grace is applied at commit, not at render.** An intermittent host
  inside the window is dropped from the pending set, the same as a host with
  no flags. A card that is already up therefore comes down through the
  existing empty-then-reap path, and its LVGL card is destroyed rather than
  orphaned. ui.c is unchanged.
- **The oldest flag decides.** The host is hidden only while *every* flag has
  a readable `since` that is no more than 7 days old. So a fresh second
  deployer cannot hide a host that the first deployer has been skipping for
  over a week.
- **An unreadable `since` voids the grace.** A flag of unknown age cannot be
  proven young, so the card shows. CD-18's rule (unknown means stale) still
  wins over the grace.
- **"More than 7 days" is exclusive.** At exactly `STALE_INTERMITTENT_GRACE_S`
  the card is still hidden, and the test pins both sides of the boundary.
- **One named constant.** `STALE_INTERMITTENT_GRACE_S` in `protocol.h` has a
  comment that names kmon's `KMON_ABSENCE_DAYS` and says to change the two
  together.
- **Testable clock.** `stale_registry_commit_cycle()` keeps its signature and
  calls the new `stale_registry_commit_cycle_at(now)`, so the tests never
  have to sleep.

## What shipped

- `src/protocol.h`: `STALE_INTERMITTENT_GRACE_S`, `INTERMITTENT_HOSTS_MAX`,
  `INTERMITTENT_HOSTS_DEFAULT`.
- `src/config.{c,h}`: `KPIDASH_INTERMITTENT_HOSTS`. The comma-list parser
  that `KPIDASH_NET_HOSTS` used inline is now `parse_host_list_env()`, shared
  by both variables. Setting either variable to an empty string means no
  hosts, not the default.
- `src/registry.{c,h}`: `stale_registry_set_intermittent()`,
  `stale_registry_commit_cycle_at()`, and the grace filter.
- `src/main.c`: hands the config list to the registry at startup.
- Tests:
  - `test_stale_card`: 7 new cases. An intermittent host's 6-day flag is
    hidden, its 8-day flag is shown, and the boundary is exclusive. An
    always-on host's 1-minute flag is shown. An unknown `since` is shown, the
    oldest deployer decides, and a live card that falls into the grace is
    reaped.
  - `test_config`: the default list, an override, and the empty-means-none
    case.
- `docs/CLIENT-PROTOCOL.md` §8c: the grace rule.
- Gates: `just check` is green (13/13 ctest, release `-O2 -Werror`, 145
  pytest, unit-lint, shell). `just check-warnings-full` is green too.

## Cross-repo changes made

- **kdashdata `29dc3b2`, on `main` and pushed.** This is a doc-only edit to
  the `kdash:stale` **Readers** paragraph in `contracts/registry.md`: it adds
  the grace rule, says the grace is reader-side only, and says an unreadable
  `since` voids it. kdashdata's `just check-docs` passed, and
  `git status -sb` shows `## main...origin/main`. kpidash's `lib/kdashdata`
  pin is unchanged at `6a928e3`. The edit is prose, and the gate builds only
  `kdash_core`, so there was no reason to move the pin.

## Follow-ups

None filed. The intermittent list now exists in two places, k-homelab
`inventory.yml` and `INTERMITTENT_HOSTS_DEFAULT`. That is accepted by WI 4111
("a kpidash config list … is acceptable"), and the comment names the source of
truth.

## Deployed

2026-10-27 18:04 PDT, from kai, to rpi53. Ran on the ship turn, after the
overseer's clearance (korg:4114 comment 4043) and the squash merge `636bea7`
(PR #27).

- **`scripts/deploy.sh` from merged `main`.** Only `kpidash.service`
  restarted; Redis did not. The service is active with `NRestarts=0`, and
  the dashboard reports version `636bea7 (2026-10-28)`, which the panel's own
  rpidash card also shows. The aarch64 link printed no warnings
  (`.scratch/deploy.log`, grepped).
- **Live check, from kai against rpi53 Redis.** Auth was resolved on the Pi
  through `kpidash-auth.sh`. The two controls behaved as expected:
  `redis-cli ping` without auth returned `NOAUTH`, and with auth returned
  `PONG`. Before the test there were no `kdash:stale:*` keys at all, so no
  real komarchy flag older than 7 days could confound it.
  1. `kdash:stale:komarchy:test` with `since` = now (`1793149499`):
     **no card**. The footer showed only the services and apt-temps cards.
  2. The same key with `since` = now − 8 d (`1792458312`): **the
     `komarchy stale` card appeared**, listing `test`, in its usual slot
     between the services and apt-temps cards.
  3. `DEL` returned 1, and 0 `kdash:stale:*` keys remain. The card was gone
     on the next screenshot.

  Each state was captured with `krpidss`: `.scratch/live-026-{recent,8d,cleared}.png`.
