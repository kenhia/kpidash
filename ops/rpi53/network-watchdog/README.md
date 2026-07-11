# rpi53 network watchdog

## Why this exists

On 2026-03-28 13:31, a manual `apt upgrade` on `rpi53` pulled in a new kernel
(`linux-image-6.12.75+rpt-rpi-v8`), WiFi/Bluetooth firmware (`firmware-brcm80211`),
`wpasupplicant`, `udev`, and `systemd-timesyncd` — none of which take effect without a reboot.
About 12 minutes later, `systemd` performed a self-reexec (`Reexecuting` in the journal),
restarting PID 1 and its managed services in place, without an actual reboot. dbus/polkit had
just finished a burst of config reloads right before this. The very next journal line is
`Journal stopped`, and nothing else was logged until a manual power-cycle on 2026-07-01 — over
three months later.

Working theory: the live re-exec restarted networking-adjacent services against the *old,
still-running* kernel while the *new* WiFi firmware/`wpasupplicant` were already on disk —
mismatched state that wedged the network stack. The dashboard's own process (C11/LVGL, no
dependency on networking) kept rendering to the physical display the whole time, which is why it
looked fine locally despite being completely unreachable over the network (and therefore reporting
itself as offline to its own dashboard, and dropping every other client's telemetry from view too).

`rpi53` has both `eth0` and `wlan0` on this network (see `nmcli device` / `ip route` — `eth0` is
the lower-metric default route), so this affected the whole network stack, not just WiFi.

## What this does

`kpidash-network-watchdog.sh`, run every ~60s via `kpidash-network-watchdog.timer`:

1. Pings the LAN default gateway (not an external host — this tests rpi53's own network stack,
   not general internet reachability).
2. On success: resets the failure counter, clears the soft-restart-already-tried flag.
3. On failure: increments a counter in `/var/lib/kpidash-network-watchdog/`.
   - At 2 consecutive failures (~2 min): restarts `NetworkManager` once (won't repeat this
     within the same outage).
   - At 10 consecutive failures (~10 min): reboots (`systemctl reboot`).

All state is logged via `logger -t kpidash-network-watchdog` — view with:

```bash
journalctl -t kpidash-network-watchdog -f
```

Thresholds are overridable via environment variables (`KPIDASH_WATCHDOG_SOFT_THRESHOLD`,
`KPIDASH_WATCHDOG_REBOOT_THRESHOLD`) if you want to tune timing without editing the script —
easiest way to set them is a systemd drop-in (`systemctl edit kpidash-network-watchdog.service`)
adding an `Environment=` line, since the `.service` file itself is managed by `install.sh`.

## Install / update

```bash
sudo ./install.sh
```

Copies the script and units into place, reloads systemd, and enables+starts the timer. Safe to
re-run to pick up changes.

## Uninstall

```bash
sudo systemctl disable --now kpidash-network-watchdog.timer
sudo rm /etc/systemd/system/kpidash-network-watchdog.{service,timer} /usr/local/bin/kpidash-network-watchdog.sh
sudo rm -rf /var/lib/kpidash-network-watchdog
sudo systemctl daemon-reload
```

## Known limitation

This detects and recovers from network-stack failures specifically. It does not protect against a
fully hung kernel (in which case the watchdog itself wouldn't run either) — that class of failure
would need the hardware watchdog (`/dev/watchdog0`, already in use per the boot log: "Using
hardware watchdog 'Broadcom BCM2835 Watchdog timer'... Watchdog running with a hardware timeout of
1min") to catch it, which is a separate, already-existing mechanism unrelated to this script.
