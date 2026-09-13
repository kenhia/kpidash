# kpidash-client systemd service

## Why this exists

`kpidash-client` has never had a systemd unit — `cli.py`'s `daemon start` only supports a manual
double-fork daemon (or `--foreground`, originally meant for debugging and required on Windows).
That means a manually-started daemon doesn't survive a reboot or a crash, and nothing restarts it
automatically. This was discovered because `rpi53` was showing "down" on its own dashboard: its
`kpidash-client` had a stale PID file from a manual `daemon start` weeks earlier, the process was
long dead, and nothing had restarted it — including after a reboot, since `kpidash.service` (the
LVGL dashboard itself) is a proper systemd unit but the client daemon never was.

This is a per-host gap: any machine running `kpidash-client` manually has the same exposure.

## Install

Requires a working checkout with `uv sync` already run (`.venv/` present) and a config already in
place at `~/.config/kpidash-client/config.toml`. From the client checkout:

```bash
./systemd/install.sh            # system unit (default) -- what rpi53 runs
./systemd/install.sh --user     # user unit             -- what kai runs
```

**`--system`** resolves the venv and config relative to wherever the checkout actually lives (host
clone paths have been observed to differ, e.g. `~/src/tools/kpidash` vs `~/src/kpidash`), generates
`/etc/systemd/system/kpidash-client.service` from the template with your user and venv path
substituted in, and enables+starts it (`Restart=always`, boot-start via `multi-user.target`).

**`--user`** installs `~/.config/systemd/user/kpidash-client.service` running
`~/.local/bin/kpidash-client` — the published package rather than a checkout venv. Both shapes are
in the fleet, so both are authored here; a unit running on a host with no copy in this repo is how
kai's diverged unnoticed.

Runs as your own user (not root) via `--foreground`, so systemd owns the process lifecycle
directly rather than the double-fork path — matches how the Windows client's `run --foreground`
works under its own service supervisor.

### Redis password

The client reads its Redis password exclusively from the `REDISCLI_AUTH` env var, and **nothing
here writes a copy of it any more.** It lives once per host in `/etc/khomelab/secrets.env`
(`root:khomelab 0640`, rendered by k-homelab from the age store) and the unit loads it with
`EnvironmentFile=`. The old per-user `~/.config/kpidash-client/redis-auth.env` is retired; its
deletion belongs to the fleet-wide changeover, not to this installer.

There is **no `-` on that `EnvironmentFile=`**, so a host without the file gets a service that
refuses to start. That is on purpose. The alternative — starting anyway with no password — is the
second bug found while fixing `rpi53`'s "down" status: the daemon came up under systemd, looked
healthy, and silently failed to authenticate to Redis, because the password it had always relied
on was never actually reaching it. A dead unit is visible; that was not.

`install.sh` checks before installing, and the check differs by shape:

- **system unit** — systemd reads `EnvironmentFile=` as PID 1, as root, *before* dropping to
  `User=`. No `khomelab` membership is involved, and `SupplementaryGroups=khomelab` must not be
  added (a group that does not exist stops the unit starting). The installer only asserts the
  file exists and grants `REDISCLI_AUTH` on this host.
- **user unit** — `systemd --user` runs as *you*, so it does need the group. Being listed in
  `/etc/group` is not enough: the manager takes its supplementary groups when it starts and, with
  `loginctl enable-linger`, outlives every login, so a membership added afterwards does not reach
  it until it restarts. The installer therefore asks the manager to attempt the read
  (`systemd-run --user ... test -r`) instead of reading `/etc/group` and inferring.

## Uninstall

```bash
sudo systemctl disable --now kpidash-client.service
sudo rm /etc/systemd/system/kpidash-client.service
sudo systemctl daemon-reload
```

## Migrating from a manual `daemon start`

If the daemon was already running via the old double-fork mode, stop it first
(`kpidash-client daemon stop`) before running `install.sh`, to avoid two instances writing to
Redis at once. Also worth clearing the stale PID file if `daemon stop` can't find the process
(`rm ~/.config/kpidash-client/daemon.pid`).
