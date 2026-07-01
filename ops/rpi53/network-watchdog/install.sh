#!/bin/bash
# Installs/updates the kpidash network watchdog on this machine. Run with sudo from the
# directory this script lives in (ops/rpi53/network-watchdog/ in the repo, or wherever
# it's been copied to on the target machine).
set -euo pipefail

if [ "$(id -u)" -ne 0 ]; then
    echo "must run as root (sudo $0)" >&2
    exit 1
fi

script_dir=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)

install -m 755 "$script_dir/kpidash-network-watchdog.sh" /usr/local/bin/kpidash-network-watchdog.sh
install -m 644 "$script_dir/kpidash-network-watchdog.service" /etc/systemd/system/kpidash-network-watchdog.service
install -m 644 "$script_dir/kpidash-network-watchdog.timer" /etc/systemd/system/kpidash-network-watchdog.timer

systemctl daemon-reload
systemctl enable --now kpidash-network-watchdog.timer

echo "Installed. Check status with:"
echo "  systemctl status kpidash-network-watchdog.timer"
echo "  journalctl -t kpidash-network-watchdog -f"
