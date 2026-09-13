# Sprint 021 — kpidash-client moves to the system unit on kai, kubs0 and kubsdb

Proposal korg:2523 (program korg:2440, "simplify homelab secrets"), covering
WI 2522. Leg `kpidash-db7095` on kai, headless, under `/overseen-sprint`.

## Goal

Sprint 020 put the Redis password in one place per host
(`/etc/khomelab/secrets.env`, `root:khomelab 0640`) and cut rpi53 over. It could
not cut over kai, kubs0 and kubsdb: all three ran `kpidash-client` as a
**lingering `systemd --user` unit**, and a user manager cannot read that file.
It is not PID 1 — it reads `EnvironmentFile=` as `ken` — and under
`loginctl enable-linger` it outlives every login, so the `khomelab` membership
added on 2026-09-12 never reached the manager that started before it.

Ken's decision (2026-09-12 18:05 PDT) was option (b): move them to the system
unit. PID 1 reads the file as root before dropping to `User=`, so the group
question disappears entirely. That voided soak WI 2508 and made this ordinary
work.

## Premise check

Every claim on WI 2522 verified, each probe run **on the host it is a claim
about** (kai locally; kubs0 and kubsdb with the command executing there):

| claim | verdict |
|---|---|
| all three run a lingering `systemd --user` unit | **holds** — active, enabled, `Linger=yes` on all three |
| none has a system unit | **holds** — no `/etc/systemd/system/kpidash-client.service` anywhere |
| the user manager cannot read the secrets file | **holds** — with a positive control (`/etc/passwd` readable) and an absent-file control |
| they run the published package, not a checkout venv | **holds** — `~/.local/bin/kpidash-client` is a uv-tool symlink on all three |
| the installer's `--system` assumes a checkout venv | **holds** — `ExecStart` was hardcoded to `$client_dir/.venv/bin` |
| `ken` is declared in `khomelab` | **holds** on all three |
| the deprecated per-user file is still there | **holds** — present on all three, and left alone (korg:2436's) |

**One thing the proposal did not say, found here:** kubs0 and kubsdb have **no
checkout of this repo at all**. So "adapt the installer to the published
package" was not sufficient — the installer could not be *run* on two of the
three hosts. That shaped the design below.

**A measurement error I made and caught.** My first probe reported
"`ken` NOT in khomelab" on kai. Bare `id -nG` reports the *calling process's*
inherited credentials, and this leg's own shell runs under the same stale user
manager — so it was reporting the bug, not the group database. `id -nG ken`
reads the database and shows the membership. Taken at face value I would have
reported that k-homelab's manifest was wrong. Same family as sprint 020's
`sudo ls /root/*` glob: the probe answered a different question than the one
being asked, and looked right doing it.

## Decisions

### Who the unit runs as: `User=ken`, and it is not merely the minimal change

WI 2522 framed this as "minimal vs. cleaner service account". Reading the code
settles it in favour of `ken` on the merits:

- The client reports **git status for the repos named in `config.toml`**, which
  on every one of these hosts are under `/home/ken/src`. A service account
  could not read them.
- `repos.py`'s `_iter_repos()` swallows `PermissionError` and returns nothing.
  So a service account would not fail — it would report **"no dirty repos"**,
  indefinitely and silently. That is precisely the failure shape this program
  keeps finding, and it would have been introduced deliberately.
- The **Redis keys are named from the hostname**, not the account
  (`redis_client.py`: `socket.gethostname()`), so nothing downstream depends on
  this choice — contrary to the note on WI 2522 that it "might" touch key names.
  It does not.
- k-homelab's `secrets_group_members` needs nothing either way: PID 1 reads the
  file. No manifest change, no new account.

### The user-unit shape is retired outright, not just refused

WI 2522 allowed either. Retiring it is the honest end state: a user unit
*cannot* read the per-host file, so a template for one is a template for a unit
that cannot work. `kpidash-client.user.service.template` is deleted, and
`install.sh --user` refuses with a message that explains why and points at the
system path. `just check-units` gained a clause that fails if any
`*.user.service.template` reappears — because the way it comes back is somebody
copying the system one "for kai".

### `--render-only`, so a host with no checkout is installable

kubs0 and kubsdb have no clone. `install.sh --render-only` prints the rendered
unit to stdout and installs nothing, so the unit is rendered where the repo is
and the bytes are delivered to the host. This keeps the **template** as the
single source of truth (so `check-units` still lints the thing that ships)
rather than embedding a unit in a script or leaving a checkout fragment on two
servers. It deliberately checks nothing about the target host — the secrets
file and `config.toml` are the target's state, not the rendering host's — and
says so on stderr.

## What shipped

- `install.sh` — one shape. `ExecStart` resolves to the published package
  (`~/.local/bin/kpidash-client`) first, the checkout venv second, `--exec`
  overriding; `--run-as` sets `User=`; `--render-only` prints without
  installing; `--user` refuses. It also retires a leftover user unit on install,
  and leaves `loginctl enable-linger` alone.
- `kpidash-client.service.template` — `__KPIDASH_CLIENT_EXEC__` replaces
  `__KPIDASH_CLIENT_VENV_BIN__`; the comment records why this is the only shape.
- `kpidash-client.user.service.template` — deleted.
- `scripts/unit-lint.sh` — drops the user template from its list, gains the
  no-user-template clause.
- `systemd/README.md` — rewritten for one shape, with an
  "Installing on a host with no checkout" section.
- `CLAUDE.md` — the note claiming kai runs a user unit was made false by this
  change; corrected.

## Verified live, and from where

Every probe ran on the host it is a claim about. The unit bytes were verified
by `md5sum` on both ends before installing (identical, `8f8cf25002...`).

Per host — kai, kubs0, kubsdb — all of the following:

- system unit `active`, `EnvironmentFiles=/etc/khomelab/secrets.env
  (ignore_errors=no)` — the missing `-` confirmed in systemd's own view, not
  just in the file;
- `systemctl --user` has no `kpidash-client`, and the user unit file is gone;
- `Linger=yes` untouched;
- exactly **one** `kpidash-client` process, running the published package;
- **the deleted-key proof**: `kpidash:client:<host>:health` deleted
  (`exists=0`), the system unit restarted, the key back with a live TTL. Old and
  new password values are identical (`76cde5f57955`), so this is the only test
  that proves the source — a running daemon or a fresh connection proves
  nothing.
- **two controls from the same host**: a wrong password → `WRONGPASS`, and no
  credential at all → `NOAUTH`. The pass means something only because both
  fail.

Fleet acceptance: all five publishers fresh at once — kai, kubs0, kubsdb, rpi53
and cleo, every health key with a live TTL.

The leg's own transient unit stayed `active` throughout: nothing here touched
the user manager, only the one unit under it.

## Repaired in passing

- **`CLAUDE.md` claimed kai's client is a user unit** and described
  `install.sh --user` as the live path. This change made both false; corrected
  in the same commit rather than left for a reader to trip over.

## Filed, with the decision named

- **`kwork` is a member of `kpidash:clients` with no client keys at all.** The
  set is `sadd`-only and never pruned, and `redis_poll()` builds a card for
  every member, marking it `online=false` when the health key is missing — so
  the panel carries a permanently-down host card, and it consumes one of
  `MAX_CLIENTS`. Not caused by this sprint and not one of its hosts. Filed
  because the fix needs a decision this sprint cannot make: whether kwork is a
  retired publisher (prune it) or a machine expected to return (in which case
  the down card is honest and the real gap is that nothing ever prunes the set).

## Left alone, deliberately

- `~/.config/kpidash-client/redis-auth.env` on all three — still present, now
  read by nothing. Deleting it is **korg:2436**. **That slice's hazard is
  lifted**: its notes said not to delete this file until WI 2508 closed, because
  the three units had no other source of the password. They now read
  `/etc/khomelab/secrets.env`, so 2436 may delete it on its ordinary host-by-host
  schedule.
- `loginctl enable-linger` on all three.
