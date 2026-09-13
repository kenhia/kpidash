# Sprint 020 — the Redis password gets one copy per host

Slice 9 of program korg:2440 ("simplify homelab secrets"), proposal korg:2428,
work item #2407. Run as karc leg `kpidash-505ce6` on kai under `/overseen-sprint`.

**Goal.** Every unit this repo authors reads `REDISCLI_AUTH` from the one
per-host file `/etc/khomelab/secrets.env` (`root:khomelab 0640`, rendered by
k-homelab from the age store), and this repo stops writing private copies of it.

---

## What the premise check found, before any code

Four claims in #2407; two held, two did not, and the two that did not are most
of this sprint.

| claim | verdict |
|---|---|
| the client unit reads `~/.config/kpidash-client/redis-auth.env` | **holds** |
| on rpi53, `kpidash.service` reads `/etc/kpidash.env` | **holds**, exactly |
| "units authored here, installed by `install.sh`" | **drifted** — see below |
| the Python client has a fallback chain to keep in step with CD-19 | **gone** — it has none |

### `kpidash.service` was not in this repo at all

The unit that supervises this project's main artifact existed only on rpi53,
hand-written, with `# Path to your binary — adjust if needed` still in it.
`scripts/deploy.sh` replaces the binary and restarts the service; it has never
touched the unit. You cannot change a unit you do not have, so the first job was
to bring it in — `ops/rpi53/dashboard/`, alongside the two ops units already
there.

### The Python client is not a CD-19 consumer, and should not become one

`kpidash_client/redis_client.py` reads `os.environ.get("REDISCLI_AUTH")` and
nothing else — same for `kpidash-mcp`. The proposal's notes say "the Python
client here is one [process that opens the per-host file itself]"; against the
code it is not, and making it one would give a daemon that systemd already hands
the value a second answer to "where is the password". Left alone, deliberately —
the same call kdashdata made for `libkdash` (handoff korg:2494, ruling 3).

What this repo *does* have is three **shell** helpers that open a password file
themselves: `scripts/krpidss`, `scripts/kpidash-cards` and `deploy.sh`'s version
readback. They have no unit to hand them a value, so they are this repo's real
CD-19 surface, and they are the ones that would have broken silently when the
changeover deletes the old file.

---

## What shipped

**`scripts/kpidash-auth.sh`** — the shell copy of CD-19, sourced by all three
helpers. Order: `$REDISCLI_AUTH` → `$KPIDASH_AUTH_FILE` (exclusive) →
`/etc/khomelab/secrets.env` → the deprecated per-user file (with a warning
naming it). Mode policy belongs to the candidate's *source*: the shared file
allows group read and refuses group write or any world bit (`mode & 0027`, so
0640 passes and 0644/0660 do not); per-user files refuse any group/other bit. On
the shared rung `EACCES` and "no `REDISCLI_AUTH=` line" mean keep looking, not
fatal — both are states of a mid-changeover fleet. A file too open is fatal on
every rung.

`krpidss` and `deploy.sh` run their snippet on the **Pi**, so the helper is
prepended to the script that is piped there and resolves the password on the Pi,
from the Pi's own file — rather than carrying a value across from the dev host.

**The units.** `kpidash.service` (new in-repo), the client system unit template,
and a **user** unit template that did not exist here before — kai, kubs0 and
kubsdb all run a user unit this repo had never authored, which is how it drifted
from the system template unnoticed.

**`install.sh`** grows `--system` (default) and `--user`, stops creating and
re-chmod'ing `redis-auth.env` entirely, and asserts before installing.

**Non-secret config out of the unit.** `KPIDASH_PRIORITY_CLIENTS` was an
`Environment=` line inside rpi53's unit — the only copy on the appliance. It is
now `/etc/kpidash/config.env`, `0644 root:root`, owned by this repo and read as a
second, optional `EnvironmentFile=-`. Program convention 3; the mode is part of
it, since a secrets mode on a file holding no secret trains a reader to look for
something that is not there.

### Two decisions worth stating

**No `-` on the secrets `EnvironmentFile=`.** kstudiodash used `-` so a host
missing the file still starts. This repo went the other way: a client or
dashboard that starts *without* a password connects, fails every write with
NOAUTH, and looks healthy doing it — which is precisely the bug recorded in
`clients/kpidash-client/systemd/README.md` ("the daemon started under systemd but
silently failed to authenticate"). On a panel with no keyboard, read from across
the room, a service that refuses to start is the only visible failure. The
installers check the file first so a bad install is caught before it is a dead
service.

**A user unit is the exception to "systemd reads it as root".** The program's
convention 2 — a unit reading `EnvironmentFile=` needs no `khomelab` membership,
because PID 1 reads it before dropping to `User=` — is true for *system* units
and false for user units. `systemd --user` runs as you. See the finding below.

---

## The finding: three of four Linux publishers cannot read the file

Measured on each host, from that host:

| host | client unit | `ken` in `khomelab` (`/etc/group`) | user manager can read the file |
|---|---|---|---|
| kai | **user**, lingering | yes | **no** |
| kubs0 | **user**, lingering | yes | **no** |
| kubsdb | **user**, lingering | yes | **no** |
| rpi53 | system | yes | n/a — PID 1 reads it as root |

The membership is declared and the manager does not have it. `systemd --user`
takes its supplementary groups when it starts, and with `loginctl enable-linger`
it outlives every login — so a group added afterwards never reaches it. On kai
the manager started 11:29 and `/etc/group` changed at 15:36; a *fresh ssh login*
on the same host has `khomelab`, the manager does not.

Asserted the honest way, by asking the manager to attempt the read rather than
reading `/etc/group` and inferring, with both controls:

```
systemd-run --user ... test -r /etc/passwd                 -> 0   (mechanism works)
systemd-run --user ... test -r /etc/khomelab/secrets.env   -> 1   (the real answer)
systemd-run --user ... test -r /etc/khomelab/not-here      -> 1   (absent)
sudo systemd-run     ... test -r /etc/khomelab/secrets.env -> 0   (what a system unit gets)
```

`install.sh --user` uses exactly that check, and prints which of the two causes
it is (not in the group at all, versus in it with a stale manager).

**So kai, kubs0 and kubsdb were not cut over.** Pointing their units at a file
they cannot read would stop telemetry on three hosts. They keep reading the
deprecated per-user file, which still works and which nothing here deletes.

**The hazard this creates for korg:2436**, stated loudly because it is not
recoverable afterwards: *do not delete
`~/.config/kpidash-client/redis-auth.env` on kai, kubs0 or kubsdb until their
units are cut over.* Deleting it on the changeover's ordinary schedule kills
kpidash telemetry on three of the five publishing hosts at once.

---

## Verified live, and from where

Every probe ran on the host it is a claim about. The dev-host helper was also
exercised from kai, where it correctly fell through to the deprecated rung and
said so.

The old and new copies of this password are **byte-identical** — fingerprint
`76cde5f57955` on `/etc/kpidash.env`, `/etc/khomelab/secrets.env` and both
per-user files, the same value k-homelab sprint 059 recorded. So a working panel
proves nothing, and neither does a fresh connection. The proofs had to be
constructed:

- **rpi53, from rpi53**: the per-host file's value → `PONG`; a deliberately
  wrong password → `WRONGPASS`; no credential at all → `NOAUTH`. A pass means
  something only because the other two fail.
- **The dashboard**: retired `/etc/kpidash.env` moved aside, `kpidash:system:version`
  **deleted**, service restarted — and the key came back. Only an authenticated
  dashboard can rewrite it, and the only remaining source was the per-host file.
  The retired file was then restored; deleting it is korg:2436's.
- **The client**: `kpidash:client:rpi53:health` deleted, service restarted, key
  back, and the running process's environ carries `REDISCLI_AUTH` at
  `76cde5f57955` with `/etc/khomelab/secrets.env` as its only `EnvironmentFile`.
- **The panel**: `krpidss` — which now carries the resolver to the Pi — returned
  a screenshot showing all five hosts, three apartment zones and the service row
  rendering live.

No value was printed at any point; fingerprints are `sha256[:12]`.

---

## Gates, and both were driven to exit 1

`just check` gains two recipes, both cheap and neither needing a Pi:

- **`just check-units`** (`scripts/unit-lint.sh`) — every unit this repo authors
  must read `/etc/khomelab/secrets.env`, must not name a retired private copy,
  and must not declare `SupplementaryGroups=khomelab`. All five clauses driven to
  exit 1 against planted units, including a missing unit file.
- **`just check-scripts`** — 12 tests over the CD-19 resolver.

Three guards negative-tested by planting the error each exists to catch: the
pre-CD-19 mode mask (refusing group read), `EACCES` treated as fatal, and
`KPIDASH_AUTH_FILE` losing its exclusivity. Each fired; each restored green.

**Worth recording: my first two plants did not fire, and the tests were right.**
One rewrote a line that changed no behaviour, the other did not match at all. A
plant that does not land looks exactly like a test that does not bite. Both were
redone with an assertion that the edit had actually applied before the test was
run.

## Repaired in passing

- **`just --list` — this repo's default recipe and its front door — printed
  garbled descriptions for five recipes.** `just` takes the *last line* of a
  multi-line comment block as the description, so `check-release` read
  "build. See CLAUDE.md's note on the warning-clean claim." and `publish` read
  "version — bump clients/kpidash-client/pyproject.toml first." Given `[doc(...)]`
  attributes. (kstudiodash hit the identical defect in sprint 007; it is a `just`
  property, not a local mistake, and worth expecting in every repo with prose
  above its recipes.)
- **`.github/copilot-instructions.md`** still stated the auth rule this sprint
  changed. Corrected. Its other drift from `CLAUDE.md` was left alone — not this
  sprint's.

## What this sprint left behind

- **WI #2508 — soak.** Cut kai, kubs0 and kubsdb over once their user managers
  restart. No amount of work here closes it: the only remedies are a reboot,
  `loginctl terminate-user`, or the ruling below — and on kai the first two kill
  the karc leg doing the work.
- **A ruling requested** on whether these three hosts should keep a lingering
  user unit at all, or move to the system unit this repo already authors — which
  PID 1 reads as root, needs no group, and would work today. Not taken
  unilaterally: it changes the supervision model on three live hosts.

## Deployed

**rpi53, 2026-09-12.** `kpidash.service` and `kpidash-client.service` both
installed from the repo, both reading `/etc/khomelab/secrets.env` and nothing
else; `KPIDASH_PRIORITY_CLIENTS` served from `/etc/kpidash/config.env`. Both
active, `NRestarts=0`, verified by the deleted-key proofs above and a live
screenshot. Previous units kept at `/root/kpidash.service.pre020` and
`/root/kpidash-client.service.pre020` for one-step rollback. The dashboard
**binary** was not rebuilt or redeployed — this sprint changed no C.

kai, kubs0 and kubsdb: **not deployed**, per the finding above.
