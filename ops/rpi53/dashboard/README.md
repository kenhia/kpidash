# rpi53 dashboard unit

## Why this exists

`kpidash.service` is what actually runs the dashboard on `rpi53`, and until
sprint 020 **this repo had no copy of it**. It was hand-written on the Pi —
comments like `# Path to your binary — adjust if needed` still in it — so the
only copy of the unit that supervises this project's main artifact lived on the
appliance it supervises. `scripts/deploy.sh` replaces the *binary* and restarts
the service; it has never touched the unit. Nothing here could tell you what the
dashboard's environment was without asking the Pi.

That is also why moving the Redis password needed this directory: you cannot
change a unit you do not have.

## What this installs

| file | installs to | mode | holds |
|---|---|---|---|
| `kpidash.service` | `/etc/systemd/system/kpidash.service` | 644 | the unit |
| `config.env` | `/etc/kpidash/config.env` | 644 root:root | non-secret settings |

The password is **not** in either, and is not in this repo. It has one copy per
host at `/etc/khomelab/secrets.env` (`root:khomelab 0640`), rendered by
k-homelab from the age store, and the unit reads it with `EnvironmentFile=`.

The split matters: `/etc/kpidash.env` used to hold the password, and
`KPIDASH_PRIORITY_CLIENTS` used to be an `Environment=` line in the unit itself.
Non-secret configuration now sits in a file this repo owns, with a mode that
says it is not a secret — a `0640 root:<group>` file holding nothing secret
trains a reader to look for something that is not there.

## Install

```sh
sudo ./install.sh
```

Run it from this directory on `rpi53`. It refuses if `/etc/khomelab/secrets.env`
is missing or grants no `REDISCLI_AUTH`, because the unit's `EnvironmentFile=`
has no `-` prefix: without the file the dashboard will not start.

That is deliberate. The panel has no keyboard and is read from across the room,
so a dashboard that starts *without* a password — connecting, authenticating
nowhere, rendering an empty screen that looks like a quiet afternoon — is the
worse failure. A service that refuses to start is the one you can see.

## Verifying

```sh
systemctl show -p EnvironmentFiles kpidash.service   # both files, secrets first
systemctl is-active kpidash.service
```

A running dashboard is **not** evidence that it picked up the new file: Redis
authenticates per connection, and the old and new copies of this password are
byte-identical, so even a fresh connection proves nothing. To actually test it,
move the retired file aside, restart, and confirm the panel still renders —
which is what sprint 020 did. Deleting the retired copy belongs to the
fleet-wide changeover, not here.
