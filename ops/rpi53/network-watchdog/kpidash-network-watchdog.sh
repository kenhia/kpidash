#!/bin/bash
# kpidash network watchdog: pings the LAN default gateway on a timer (via the paired
# .timer unit). On sustained failure it first restarts NetworkManager, then reboots if
# that doesn't recover things in time.
#
# Written after rpi53 went unreachable for ~3 months (2026-03-28 to 2026-07-01): an `apt
# upgrade` pulled in a new kernel, brcm80211 WiFi/BT firmware, wpasupplicant, and udev,
# none of which take effect without a reboot. A `systemd` self-reexec ~12 minutes later
# restarted networking-adjacent services live against the still-running old kernel --
# journald stopped logging immediately after and nothing else was recorded until a manual
# power-cycle. The dashboard's own C/LVGL process kept rendering the whole time (no
# dependency on networking), which is why it looked "up" locally despite being
# unreachable. See ops/rpi53/network-watchdog/README.md for the full writeup.

set -euo pipefail

STATE_DIR="/var/lib/kpidash-network-watchdog"
FAIL_COUNT_FILE="$STATE_DIR/fail_count"
SOFT_RESTART_DONE_FILE="$STATE_DIR/soft_restart_done"
LOG_TAG="kpidash-network-watchdog"

# Consecutive failed checks (at the timer's ~60s interval) before each escalation tier.
SOFT_RESTART_THRESHOLD="${KPIDASH_WATCHDOG_SOFT_THRESHOLD:-2}"   # ~2 min
REBOOT_THRESHOLD="${KPIDASH_WATCHDOG_REBOOT_THRESHOLD:-10}"      # ~10 min

mkdir -p "$STATE_DIR"

gateway=$(ip route | awk '/^default/ { print $3; exit }')
if [ -z "$gateway" ]; then
    logger -t "$LOG_TAG" "no default gateway found in routing table, skipping this check"
    exit 0
fi

if ping -c 1 -W 3 "$gateway" >/dev/null 2>&1; then
    prev_count=$(cat "$FAIL_COUNT_FILE" 2>/dev/null || echo 0)
    if [ "$prev_count" -gt 0 ]; then
        logger -t "$LOG_TAG" "gateway $gateway reachable again after $prev_count failed check(s)"
    fi
    echo 0 > "$FAIL_COUNT_FILE"
    rm -f "$SOFT_RESTART_DONE_FILE"
    exit 0
fi

count=$(( $(cat "$FAIL_COUNT_FILE" 2>/dev/null || echo 0) + 1 ))
echo "$count" > "$FAIL_COUNT_FILE"
logger -t "$LOG_TAG" "gateway $gateway unreachable ($count consecutive failed check(s))"

if [ "$count" -ge "$REBOOT_THRESHOLD" ]; then
    logger -t "$LOG_TAG" "network down for $count consecutive checks (>= $REBOOT_THRESHOLD), rebooting"
    systemctl reboot
    exit 0
fi

if [ "$count" -ge "$SOFT_RESTART_THRESHOLD" ] && [ ! -f "$SOFT_RESTART_DONE_FILE" ]; then
    logger -t "$LOG_TAG" "network down for $count consecutive checks (>= $SOFT_RESTART_THRESHOLD), restarting NetworkManager"
    touch "$SOFT_RESTART_DONE_FILE"
    systemctl restart NetworkManager || logger -t "$LOG_TAG" "NetworkManager restart failed"
fi
