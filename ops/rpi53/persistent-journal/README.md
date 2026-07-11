# rpi53 persistent journal logging

## Why this exists

Investigating the (recurring, ~3rd time this week as of 2026-07-02) network outage that makes
`rpi53` unreachable, we found there was **no log evidence at all** from before the reboot that
fixed each incident — `journalctl -b -1` kept returning the same stale entries from 2026-03-28,
no matter how many reboots happened since.

Root cause: `rpi53` had `Storage=volatile` in effect for `systemd-journald`, shipped by the
Raspberry Pi Foundation's own `raspberrypi-sys-mods` package
(`/usr/lib/systemd/journald.conf.d/40-rpi-volatile-storage.conf`) — a stock Raspberry Pi OS
default, not something any agent "hardened". It exists to reduce SD card wear (this Pi actually
boots from NVMe, so the tradeoff doesn't even apply here). With `Storage=volatile`, journald only
logs to a RAM-backed ring buffer (`/run/log/journal/`) that's wiped on every reboot — so every time
something went wrong badly enough to need a reboot, the reboot itself destroyed the evidence.

This also means **our own `kpidash-network-watchdog`'s activity log has never actually been
verifiable** — its `logger -t kpidash-network-watchdog` calls were subject to the exact same
wipe, so there's no way to know from past incidents whether it detected the outage, tried a soft
restart, or gave up and rebooted. Going forward, with persistent storage, its logs (and everything
else) will survive across the outage/reboot boundary.

## What this does

`50-persistent-storage.conf` → `/etc/systemd/journald.conf.d/50-persistent-storage.conf`: a local
override with `Storage=persistent`. Local drop-ins in `/etc/systemd/journald.conf.d/` take
precedence over vendor-shipped ones in `/usr/lib/systemd/journald.conf.d/` when sorted together by
filename (`50-` sorts after `40-`), so this correctly wins over the RPi default without needing to
touch or remove the vendor file.

## Install

```bash
sudo ./install.sh
```

Installs the drop-in and restarts `systemd-journald` immediately (no reboot needed). Verify with:

```bash
systemd-analyze cat-config systemd/journald.conf | grep -A1 'Storage='
ls -lat /var/log/journal/*/ | head -3   # should show a recent timestamp, not a stale one
```

## Uninstall

```bash
sudo rm /etc/systemd/journald.conf.d/50-persistent-storage.conf
sudo systemctl restart systemd-journald
```

(Reverts to the RPi OS default of `Storage=volatile`.)

## Next time this recurs

With this fix in place, `journalctl -b -1` after the next reboot should actually show what
happened leading up to it — check that first, rather than assuming logs are unavailable again.
