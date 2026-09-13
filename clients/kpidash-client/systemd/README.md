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

## One shape: a system unit

Until sprint 021 this installer offered two shapes, and the fleet ran both: a system unit on
rpi53, and a `systemd --user` unit on kai, kubs0 and kubsdb. **The user shape is retired**, and
`--user` is now refused with a message explaining why.

A user unit cannot read `/etc/khomelab/secrets.env`. `systemd --user` is not PID 1: it runs as you
and reads `EnvironmentFile=` with your credentials, and under `loginctl enable-linger` the manager
outlives every login — so a `khomelab` membership added after it started never reaches it. The
membership can be correct in every file on disk and still not be in effect, indefinitely, and
`id -nG ken` will not show you that. A system unit sidesteps the whole question, because PID 1
reads the file as root before dropping to `User=`.

## Install

Requires a config already in place at `~/.config/kpidash-client/config.toml`. From the client
checkout:

```bash
./systemd/install.sh
```

That generates `/etc/systemd/system/kpidash-client.service` from the template, enables and starts
it (`Restart=always`, boot-start via `multi-user.target`), and retires any user unit this host was
running. `loginctl enable-linger` is deliberately left alone — other things run under that
manager.

**Which binary it runs.** The published package (`~/.local/bin/kpidash-client`, a uv tool
installed with `kpkg install kpidash-client`) is preferred; a checkout venv
(`<checkout>/.venv/bin/kpidash-client`) is the fallback. A host with both is a dev host, and the
published build is the one the fleet actually runs. `--exec PATH` forces the choice.

**Which account it runs as.** Your own, by default (`--run-as NAME` overrides). Not root, and
deliberately not a dedicated service account: the client reports git status for the repos named in
`config.toml`, which live in your home, and `repos.py` swallows `PermissionError` — so a service
account would report "no dirty repos" rather than failing. The Redis keys are named from the
hostname, not the account, so nothing downstream depends on this choice.

Runs via `--foreground`, so systemd owns the process lifecycle directly rather than the
double-fork path — matches how the Windows client's `run --foreground` works under its own service
supervisor.

## Installing on a host with no checkout

kubs0 and kubsdb run the published package and have no clone of this repo. Render the unit where
the repo *is*, deliver the bytes, install them there:

```bash
# on a host with the checkout
./systemd/install.sh --render-only > /tmp/kpidash-client.service

# deliver and install (base64 so no shell mangles the content in transit)
base64 -w0 /tmp/kpidash-client.service |
    ssh HOST 'base64 -d | sudo install -m 644 /dev/stdin /etc/systemd/system/kpidash-client.service'
ssh HOST 'sudo systemctl daemon-reload && sudo systemctl enable --now kpidash-client.service'
```

`--render-only` checks nothing about the target host — the secrets file and `config.toml` are its
state, not the rendering host's — and says so on stderr. Verify there, after installing.

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

`install.sh` checks before installing: systemd reads `EnvironmentFile=` as PID 1, as root,
*before* dropping to `User=`, so no `khomelab` membership is involved and
`SupplementaryGroups=khomelab` must not be added (a group that does not exist stops the unit
starting). The installer asserts the file exists and grants `REDISCLI_AUTH` on this host.

`just check-units` enforces both of those against the template, and fails if a
`*.user.service.template` reappears anywhere in the repo — which is how the retired shape would
come back, somebody copying the system one "for kai".

## Uninstall

```bash
sudo systemctl disable --now kpidash-client.service
sudo rm /etc/systemd/system/kpidash-client.service
sudo systemctl daemon-reload
```

To retire a leftover user unit by hand (the installer does this for you):

```bash
systemctl --user disable --now kpidash-client.service
rm ~/.config/systemd/user/kpidash-client.service
systemctl --user daemon-reload
```

Leave `loginctl enable-linger` alone.

## Migrating from a manual `daemon start`

If the daemon was already running via the old double-fork mode, stop it first
(`kpidash-client daemon stop`) before running `install.sh`, to avoid two instances writing to
Redis at once. Also worth clearing the stale PID file if `daemon stop` can't find the process
(`rm ~/.config/kpidash-client/daemon.pid`).
