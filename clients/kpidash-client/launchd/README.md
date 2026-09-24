# kpidash-client under launchd (macOS)

The macOS counterpart of [`../systemd/`](../systemd/README.md), added in sprint 023
(WI #3132) for kimac, the fleet's first Mac. It follows the pattern kmon set for the
fleet's Macs in kmon sprint 39.

| | |
|---|---|
| **Label** | `net.kenhia.kpidash.client` |
| **Template** | `launchd/net.kenhia.kpidash.client.plist`. `@HOME@` and `@EXEC@` are filled at install, because launchd expands neither `~` nor `$HOME` |
| **Installed** | `~/Library/LaunchAgents/net.kenhia.kpidash.client.plist`, domain `gui/$(id -u)` |
| **Log** | `~/Library/Logs/kpidash/client.log` (stdout and stderr) |
| **Shape** | `RunAtLoad` + `KeepAlive`: a daemon, restarted by launchd if it exits |
| **Runs** | `~/.local/bin/kpidash-client daemon start --foreground` (the published package, a uv tool on uv's Python), else this checkout's `.venv` |
| **Asserted by** | `launchctl print gui/<uid>/net.kenhia.kpidash.client`: rc 0 loaded, rc 113 not there |

```bash
./launchd/install.sh install     # render, plutil -lint, bootstrap (idempotent)
./launchd/install.sh status      # launchctl print
./launchd/install.sh uninstall   # bootout, wait until gone, remove the plist
./launchd/install.sh render --home /Users/ken   # print it; works on any host
```

`install` wants `~/.config/kpidash-client/config.toml` in place, and
`/etc/khomelab/secrets.env` readable with a `REDISCLI_AUTH` line in it. It checks both
before loading anything.

## Why a per-user agent is fine here when a `systemd --user` unit was not

Sprint 021 retired the `systemd --user` shape because a lingering user manager keeps
the groups it started with, so it never saw `khomelab`. launchd does not work that
way. It resolves a job's supplementary groups **when it spawns the job**. kmon
sprint 39 proved this on kimac: a throwaway agent, fired by launchd, printed
`groups=…501(khomelab)…` and read the file, even though `khomelab` was added after Ken
logged in. So a LaunchAgent can read `root:khomelab 0640` directly, and it gets
nothing from a LaunchDaemon that would be worth running as root.

## The password

launchd has no `EnvironmentFile=`. The plist's `ProgramArguments` is therefore a
short `/bin/sh` launcher. It pulls `REDISCLI_AUTH` out of `/etc/khomelab/secrets.env`
(`KEY='value'` or bare, first match, never eval'd, the same extraction as
`scripts/kpidash-auth.sh`), exports it, and `exec`s the client. The Python client still
reads `$REDISCLI_AUTH` and nothing else.

If no value is readable, the launcher exits 78 and the client is never started. That
is the twin of the systemd unit's un-prefixed `EnvironmentFile=`. launchd will retry
every 10 s, and each retry writes one line to the log. `just check-units` fails if the
plist stops naming the file, names a retired private copy, or grows an
`EnvironmentVariables` dict.

## Installing on a Mac with no checkout

Same as the Linux route. Render where the repo is, deliver the bytes, bootstrap there:

```bash
./launchd/install.sh render --home /Users/ken > /tmp/net.kenhia.kpidash.client.plist
base64 -w0 /tmp/net.kenhia.kpidash.client.plist |
    ssh kimac 'base64 -d > ~/Library/LaunchAgents/net.kenhia.kpidash.client.plist'
ssh kimac 'mkdir -p ~/Library/Logs/kpidash && plutil -lint ~/Library/LaunchAgents/net.kenhia.kpidash.client.plist &&
    launchctl bootstrap gui/$(id -u) ~/Library/LaunchAgents/net.kenhia.kpidash.client.plist'
```

## Sleep

An agent needs nothing special for sleep. The process is suspended along with the
Mac. The health key (5 s TTL) and the telemetry key (15 s) expire, and the panel's card
goes to **asleep**, provided `config.toml` says `availability = "intermittent"` (see
the client README). On wake, the same process resumes publishing within one health
interval. On kimac it was measured with the same PID and `runs = 1`, so launchd did not
restart it.

**Note:** a Mac put to sleep with `pmset sleepnow` answers network traffic with a
*DarkWake* and then goes back to sleep. An ssh probe wakes it briefly and then loses
it again. `caffeinate -u -t 2` declares user activity and brings it back to full wake.
