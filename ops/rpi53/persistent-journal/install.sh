#!/bin/bash
# Overrides Raspberry Pi OS's stock journald default (Storage=volatile, shipped by the
# raspberrypi-sys-mods package at /usr/lib/systemd/journald.conf.d/40-rpi-volatile-storage.conf,
# to reduce SD card wear) so logs survive a reboot. See this directory's README.md for why this
# matters: every reboot was silently destroying all evidence of whatever caused it, including our
# own network watchdog's activity log.
set -euo pipefail

if [ "$(id -u)" -ne 0 ]; then
    echo "must run as root (sudo $0)" >&2
    exit 1
fi

script_dir=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)

mkdir -p /etc/systemd/journald.conf.d
install -m 644 "$script_dir/50-persistent-storage.conf" /etc/systemd/journald.conf.d/50-persistent-storage.conf
systemctl restart systemd-journald

echo "Installed. Verify with:"
echo "  systemd-analyze cat-config systemd/journald.conf | grep -A1 Storage="
echo "  ls -lat /var/log/journal/*/ | head -3   # should show a recent timestamp, not a stale one"
