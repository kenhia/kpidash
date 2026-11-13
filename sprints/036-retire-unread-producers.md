# 036 — Retire the client's unread producers, release 2.0.0

Proposal korg:4596 covers WI 4563. Slice 1 of program korg:4599 ("Retire
kpidash's unread producers"). It ran as a karc leg (kpidash-669d1d) on kai
under an overseer.

## Goal

Sprint 032 removed the Activities, Repo Status and Fortune widgets and every
read of their keys. The producers in this repo kept writing them to rpi53's
Redis: the daemon's repo scanner (`kpidash:repos:{host}`, every ~30 s on every
client host), `kpidash-client activity start|done` (`kpidash:activities`,
`kpidash:activity:{uuid}`), `kpidash-client fortune push`
(`kpidash:fortune:pushed`), and the `kpidash-mcp` package, which was retired
from the fleet in korg:2381 but still in the tree.

Ken's decision (WI 4563, comment 4633, "File and start"): drop them from the
published client, fleet-wide, now. Delete `clients/kpidash-mcp` outright
(retrievable from tag `last-with-dev-graphs-fortune`), release as 2.0.0 since
removing CLI commands is breaking, and do not chase hosts that are asleep or
off: k-homelab's raised version floor (slice 2) catches them.

`dev_telemetry` is not in scope. kdeskdash still reads it.

## Caller re-check

The overseer's fleet search had hit its cap on kai, so it was re-run before
anything was deleted, narrower and untruncated:

- **kaed `*:*`** (8 roots on kai, kubs0, kubsdb; 77,263 files, no root
  truncated), for `kpidash-client activity|fortune` and
  `kpidash:(repos|activities|activity:|fortune)`. Outside this repo: only
  documentation. kdashdata's `contracts/registry.md` and its README example
  (slice 3's job), the same registry vendored into kdeskdash's
  `lib/kdashdata`, a k-homelab sprint record, and a kwebi survey page. No
  code reads or writes the keys, and nothing calls the commands.
- **Off-root surfaces, over ssh from kai**: `~/.claude` settings, hooks and
  skills, shell profiles, user and system systemd units, cron, and
  `/usr/local/bin` on kai, kubs0, kubsdb, gratch, rpi53 and kimac (plus
  `~/Library/LaunchAgents` on kimac); cleo's Claude config. Also agent-skills
  on kubs0 (`7f0f0b6`). One hit: a July copy of `sprint-ship/SKILL.md` in
  kubsdb's `~/.claude/skills`, which makes a guarded, optional call to
  `mcp__kpidash__start_activity`. agent-skills' `fleet.yml` says kubsdb runs
  no agent and gets no skills, and kubsdb's `~/.claude.json` registers no
  kpidash MCP server. So it is a leftover on a host where no agent runs, and
  it calls a tool already retired everywhere. This sprint changes nothing
  about it. Flagged to the overseer, not edited: it is another repo's
  deployment state on another host.

No caller, so the removal went ahead.

## What changed

- **Client code.** `repos.py` and its test are gone. So are `write_repos`,
  `start_activity`, `end_activity` and `push_fortune` in `redis_client.py`,
  the daemon's `_repo_loop`, and the `activity` and `fortune` CLI groups with
  the `state.json` helpers only `activity` used. `GitPython` went from the
  dependencies with the scanner, which was its only user (`uv.lock`
  re-locked; `gitpython` and `smmap` dropped).
- **Config tolerance.** `RepoConfig`, `ClientConfig.repos` and
  `repo_scan_interval_s` are gone, and a leftover `[repos]` section or
  `[client] repo_scan_interval_s` is now ignored without a warning. TOML
  parsing never rejected unknown keys. The parser just stopped reading these.
  The silence is on purpose: k-homelab keeps rendering both until slice 2,
  and a host can be upgraded before or after its config changes, so a warning
  would fire on every start of every host about a file the host does not own.
  `test_a_leftover_repos_section_is_ignored_silently` pins it, with the exact
  shape k-homelab renders and an empty `caplog` at DEBUG.
- **`clients/kpidash-mcp`** deleted. The tag
  `last-with-dev-graphs-fortune` (`43e7937`, on origin) has it.
- **Version 2.0.0** in `clients/kpidash-client/pyproject.toml`.
- **Tests.** `test_activity.py` and `test_repos.py` deleted, and the
  activity/fortune cases removed from `test_redis_client.py`. New
  `tests/test_cli.py` asserts `activity` and `fortune` are no longer commands
  and the remaining ones still are.
- **Docs.** `CLIENT-PROTOCOL.md` §4-6 collapse into one "retired, producers
  and all" note, keeping the section numbers so everything after stays stable.
  The schemas are on the tag. `ARCHITECTURE.md` § Removed widgets gains a
  producers table with paths at the tag, and its client diagram and tree lose
  `write_repos`, the retired CLI and `kpidash-mcp`. The root README, the
  client README (new "Removed in 2.0.0" section; the config example loses
  `[repos]` and says a leftover one is ignored), CLAUDE.md, and the
  historical note at the top of `HANDOFF-CROSSCOMPILE.md` all follow.
  `systemd/README.md`'s reason for running as the user cited the repo
  scanner. It now gives the reason that remains (config in your home), and
  the default is unchanged.

There is no CI and the justfile never referenced `kpidash-mcp`, so neither
needed a change.

## Gate

`just check` rc 0: ctest 16/16, check-release clean, ruff clean, pytest 159
passed, unit-lint, the auth and font-coverage shell tests.

## Repaired in passing

- **`.github/copilot-instructions.md` § Project had drifted** several sprints
  behind CLAUDE.md's (the gate description, the opaque-fill note, the
  secrets-file history and more, about 109 diff lines). It is the same
  section, meant to mirror. Re-synced from CLAUDE.md, which is newer
  everywhere. The managed kproject block above it is untouched.
- **`sprints/planning/roadmap.md` "Now"** still showed 034 as current, with
  035 shipped and missing. Brought current.

## Rollout (the ship turn, after the overseer's clearance)

Not done in this turn: no publish, no upgrades, no key deletion.

Client hosts from rpi53's `kpidash:clients` set and live health keys, cross-
checked against k-homelab's manifests (`kpidash-client` in kai, kubs0, kubsdb,
gratch and kimac; rpi53's manifest asserts the unit). All probed from kai,
which does the rollout, on 2026-11-13 around 11:20 PST:

| host | installed | managed by | unit |
|---|---|---|---|
| kai | 1.3.1 | uv tool (`~/.local/bin`) | system `kpidash-client.service`, active |
| kubs0 | 1.3.1 | uv tool | system unit, active |
| kubsdb | 1.3.1 | uv tool | system unit, active |
| gratch | 1.5.0 | uv tool | system unit, active |
| rpi53 | 1.3.1 (checkout at `918f2ba`) | source venv `~/src/kpidash/clients/kpidash-client/.venv` | system unit, active |
| kimac | 1.2.0 | uv tool | LaunchAgent `net.kenhia.kpidash.client`, running |

Not upgraded, and why:

- **cleo** publishes health through the separate Windows client and is
  untouched.
- **kwork** is a member of `kpidash:clients` with no health key (TTL -2), no
  k-homelab manifest, and `ssh: connect … Connection timed out` from kai. It
  is not one of the six and runs no `kpidash-client` the fleet manages. It is
  listed, not chased.

Orphaned keys on rpi53 at the time of the check: `kpidash:repos:kai`, 63 keys
matching `kpidash:activit*`, and `kpidash:fortune:current`.

## Deployed

2026-11-13, 11:01–11:07 PST, from kai under the overseer's clearance
(korg:4596 comment 4638). Merged as `635efce` (#39). There is no CI.

- **Published** with `just publish`: `kpidash_client-2.0.0` wheel and sdist
  are in the package store on kubsdb.
- **Upgraded one host at a time.** After each restart I checked on rpi53 that
  the health TTL had reset, telemetry was fresh, `kdash:net` was under 3 s old
  and `kpidash:repos:{host}` was absent.

  | host | from → to | how | after restart |
  |---|---|---|---|
  | kai | 1.3.1 → 2.0.0 | `uv tool upgrade`, restart system unit | health 5, telemetry 13, net 2.3 s |
  | kubs0 | 1.3.1 → 2.0.0 | same | health 5, telemetry 13, net 2.4 s |
  | kubsdb | 1.3.1 → 2.0.0 | same | health 5, telemetry 13, net 2.2 s |
  | gratch | 1.5.0 → 2.0.0 | same | health 5, telemetry 13, net 2.3 s, nas 2.3 s |
  | kimac | 1.2.0 → 2.0.0 | `uv tool upgrade`, `launchctl kickstart -k` | health 4, telemetry 14 (no net on macOS, before or after) |
  | rpi53 | 1.3.1 → 2.0.0 | WI 1427's route: rsync of kai's checkout, `uv sync`, restart | health 5, telemetry 13, net 2.1 s |

  Each unit came back `active` with `NRestarts=0`, and no host's journal had
  an error. The old daemon on kubs0 had used 3 h 9 min of CPU, much of it
  presumably the repo scanner walking `~/src`.
- **rpi53's checkout** cannot pull from GitHub (no key), so it is synced from
  kai. Its "modified" `lib/kdashdata` turned out to be two tracked kdashdata
  files (`scripts/build-darwin.sh`, `scripts/build-darwin-remote.sh`) missing
  from the working tree. A dry run showed that `--delete` would remove only
  files upstream had removed since `918f2ba`, two untracked 1.1.0 `dist/`
  artifacts and a stale remote-tracking ref. There were no local commits and
  no stash, so nothing unique was lost. It now reads `## main...origin/main`
  at `635efce`, clean.
- **Orphaned keys** deleted on rpi53's Redis with SCAN + DEL: 64 keys (62
  `kpidash:activity:*` hashes, `kpidash:activities`,
  `kpidash:fortune:current`). `kpidash:repos:kai` had already expired once kai
  stopped writing it. **0 after 65 s.**
- **Not upgraded:** cleo, whose Windows client is a separate package and was
  out of scope, and kwork, a stale member of `kpidash:clients` with no
  manifest that is unreachable from kai. The overseer ruled to leave it.
