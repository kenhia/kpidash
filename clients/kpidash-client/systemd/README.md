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
./systemd/install.sh
```

This resolves the venv and config relative to wherever the checkout actually lives (host clone
paths have been observed to differ, e.g. `~/src/tools/kpidash` vs `~/src/kpidash`), generates
`/etc/systemd/system/kpidash-client.service` from the template with your user and venv path
substituted in, and enables+starts it (`Restart=always`, boot-start via `multi-user.target`).

Runs as your own user (not root) via `--foreground`, so systemd owns the process lifecycle
directly rather than the double-fork path — matches how the Windows client's `run --foreground`
works under its own service supervisor.

### Redis password

The client reads its Redis password exclusively from the `REDISCLI_AUTH` env var — but systemd
services start with a clean environment, so they don't see whatever your `~/.bashrc` exports for
interactive shells. `install.sh` handles this: if `~/.config/kpidash-client/redis-auth.env`
doesn't exist yet, it creates it (mode 600) from `REDISCLI_AUTH` in the shell you're running the
installer from, and the unit loads it via `EnvironmentFile=`. If `REDISCLI_AUTH` isn't set in your
shell either, create the file yourself first: `echo 'REDISCLI_AUTH=<password>' > \
~/.config/kpidash-client/redis-auth.env && chmod 600 ~/.config/kpidash-client/redis-auth.env`.

(This was the second bug found while fixing `rpi53`'s "down" status — the daemon started under
systemd but silently failed to authenticate to Redis, since the password it had always relied on
was never actually reaching it.)

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
