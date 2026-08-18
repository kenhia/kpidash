# kpidash-client

A telemetry and activity daemon that reports machine status to the
[kpidash](https://github.com/your-org/kpidash) dashboard display.

---

## Requirements

- Python 3.11+
- Redis server accessible from this machine (running on the Pi 5 dashboard host)
- `REDISCLI_AUTH` environment variable set to the Redis password (if any)

---

## Installation

```bash
# from source (uv recommended — ensures Python 3.13):
cd /path/to/kpidash/clients/kpidash-client
uv sync                        # creates .venv with Python 3.13, installs deps
uv run kpidash-client --help   # verify
```

---

## Configuration

### Where Redis is

On the homelab tailnet the Redis endpoint comes from
[khlenv](https://github.com/kenhia/khlenv), the config resolver: leave
`[redis]` out of `config.toml` entirely and the client asks khlenv for
`KPIDASH_REDIS` on every connect. Moving Redis is then one edit to the
khlenv store, not a config edit on every reporting host — and a reconnect
after a failed write re-asks, so the move propagates by itself.

khlenv resolves the **endpoint only**. The password stays in
`REDISCLI_AUTH`; khlenv never holds secrets.

If khlenv is unreachable the client uses its last cached answer (with a
loud warning naming the cache age), and failing that a compiled-in
`rpi53:6379`. Off the tailnet — or anywhere you want to pin the endpoint —
keep `[redis] host` in `config.toml`: it is an explicit override and wins
outright, port included.

**Prerequisite: the host must resolve tailnet names.** khlenv's built-in
endpoint is the MagicDNS name `kubs0.encke-wahoo.ts.net`, so a host with
Tailscale DNS off (`CorpDNS: false` in `tailscale debug prefs`) cannot
reach it — and cannot reach the homelab package index that ships the
`khlenv` wheel either. Fix it at the source with
`sudo tailscale set --accept-dns=true`; the per-machine escape hatch is an
endpoint override in `/etc/khlenv/endpoint`.

### Linux / macOS

Create `~/.config/kpidash-client/config.toml`:

```toml
# [redis] is optional on the homelab tailnet — omit it to resolve the
# endpoint through khlenv. Set it to pin the endpoint locally instead.
[redis]
host = "192.168.1.50"   # Pi 5 IP address
port = 6379             # default

[client]
hostname = ""           # optional: override auto-detected hostname
telemetry_interval_s = 5
health_interval_s    = 3
repo_scan_interval_s = 30

[[disks]]
path  = "/"
label = "root"
# type is auto-detected on Linux (ssd/hdd/nvme via /sys/block)

[[disks]]
path  = "/mnt/data"
label = "data"

[repos]
explicit   = ["/home/user/important-repo"]
scan_roots = ["/home/user/src"]
scan_depth = 3
exclude    = ["/home/user/src/vendor"]
```

### Windows

Create `%APPDATA%\kpidash-client\config.toml`:

```toml
[redis]
host = "192.168.1.50"
port = 6379

[client]
telemetry_interval_s = 5

[[disks]]
path  = "C:\\"
label = "C"
type  = "ssd"   # required on Windows (auto-detection not supported)

[[disks]]
path  = "D:\\"
label = "D"
type  = "hdd"
```

> **Note**: On Windows, disk type must be specified explicitly — `/sys/block`
> auto-detection is Linux-only.

### Environment variables

| Variable | Required | Description |
|----------|----------|-------------|
| `REDISCLI_AUTH` | Yes (if Redis has a password) | Redis authentication password |

---

## Daemon commands

```bash
# Start daemon in background (writes PID to ~/.config/kpidash-client/daemon.pid)
kpidash-client daemon start

# Run in foreground (useful for debugging; required on Windows)
kpidash-client daemon start --foreground

# Stop the daemon
kpidash-client daemon stop
```

---

## Activity tracking

```bash
# Start a named activity (saves activity_id to state.json automatically)
kpidash-client activity start "Implementing kpidash MVP"

# Mark the last activity as done
kpidash-client activity done

# End a specific activity by ID
kpidash-client activity done 550e8400-e29b-41d4-a716-446655440000
```

---

## Fortune messages

```bash
# Push a custom fortune to the dashboard (visible for 5 minutes by default)
kpidash-client fortune push "Stay curious."

# Override display duration
kpidash-client fortune push "Deploying to prod on Friday." --duration 600
```

---

## Status / log path

```bash
# Acknowledge a dashboard warning or error message
kpidash-client status ack

# Print the dashboard's log file path (then scp or tail it remotely)
kpidash-client log-path
# Example: /var/log/kpidash/kpidash.log
```

---

## Windows notes

1. Install Python 3.11+: https://python.org/downloads/
2. Open PowerShell and install:
   ```powershell
   pip install kpidash-client
   ```
3. Set the Redis password in your PowerShell session:
   ```powershell
   $env:REDISCLI_AUTH = "yourpassword"
   ```
4. Create the config file at `%APPDATA%\kpidash-client\config.toml` (see above).
5. Run the daemon in the foreground for MVP (background service setup
   requires Task Scheduler or a wrapper — out of scope for MVP):
   ```powershell
   kpidash-client daemon start --foreground
   ```

---

## Development

```bash
cd /path/to/kpidash/clients/kpidash-client
uv sync
uv run ruff format --check . && uv run ruff check . && uv run pytest -q
```
