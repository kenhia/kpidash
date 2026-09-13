#!/bin/bash
# Installs kpidash-client as a systemd service on this machine, so it starts on boot and
# restarts automatically on crash -- rather than the manual `daemon start` double-fork
# mode (cli.py's `daemon_start()`), which doesn't survive a reboot or process death.
#
# Two shapes, because the fleet runs both:
#
#   --system  (default)  /etc/systemd/system/kpidash-client.service, User=<you>,
#                        ExecStart from this checkout's venv. What rpi53 runs.
#   --user               ~/.config/systemd/user/kpidash-client.service, ExecStart
#                        from ~/.local/bin (the published package). What kai runs.
#
# Run as the user that owns this kpidash-client checkout (NOT root -- sudo is used
# internally for the parts that need it). Locates the checkout and venv relative to this
# script's own location, so it works regardless of where the repo was cloned on a given
# host (observed to differ across machines, e.g. ~/src/tools/kpidash vs ~/src/kpidash).
#
# The Redis password is NOT written here any more. It lives once per host in
# /etc/khomelab/secrets.env, rendered by k-homelab from the age store, and the unit
# reads it with EnvironmentFile=. This script asserts that it will actually be
# readable by the supervisor that will run the unit, which is not the same question
# for the two shapes -- see check_secrets_readable() below.
set -euo pipefail

SECRETS_FILE=/etc/khomelab/secrets.env
MODE=system

while [ $# -gt 0 ]; do
    case "$1" in
        --system) MODE=system; shift ;;
        --user)   MODE=user; shift ;;
        -h|--help) sed -n '2,20p' "$0"; exit 0 ;;
        *) echo "unknown arg: $1 (try --help)" >&2; exit 1 ;;
    esac
done

if [ "$(id -u)" -eq 0 ]; then
    echo "run this as the user who owns the kpidash-client checkout, not root" >&2
    exit 1
fi

script_dir=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
client_dir=$(cd "$script_dir/.." && pwd)
venv_bin="$client_dir/.venv/bin"

if [ ! -f "$HOME/.config/kpidash-client/config.toml" ]; then
    echo "no config at ~/.config/kpidash-client/config.toml -- create one before installing" >&2
    exit 1
fi

# --- the password, and who can read it -----------------------------------------------
#
# The unit has no `-` on its EnvironmentFile=, so systemd refuses to start it if the
# file cannot be read. That is deliberate (a client with no password connects and fails
# every write with NOAUTH while looking healthy), but it means a bad install is a dead
# service rather than a warning -- so check it HERE, before installing anything.
check_secrets_readable() {
    if [ ! -f "$SECRETS_FILE" ]; then
        echo "$SECRETS_FILE does not exist on this host." >&2
        echo "It is rendered by k-homelab's khomelab-secrets recipe -- run 'bin/apply <host> khomelab-secrets'" >&2
        echo "from the k-homelab control node before installing." >&2
        return 1
    fi

    case "$MODE" in
    system)
        # systemd reads EnvironmentFile= as PID 1, as root, BEFORE dropping to User=.
        # So the unit's own account needs no khomelab membership, and must not be given
        # SupplementaryGroups=khomelab (a group that does not exist would stop the unit
        # starting). Root can always read it; what is worth asserting is that the key
        # this client needs is actually present on this host -- the per-host file holds
        # only the keys that host's manifest grants.
        if ! sudo grep -q '^[[:space:]]*REDISCLI_AUTH=' "$SECRETS_FILE"; then
            echo "$SECRETS_FILE has no REDISCLI_AUTH -- this host's k-homelab manifest does not" >&2
            echo "grant it. Add REDISCLI_AUTH to the host's secrets: key list and re-apply." >&2
            return 1
        fi
        ;;
    user)
        # A user unit is NOT started by PID 1. `systemd --user` runs as you and reads
        # EnvironmentFile= with your credentials, so this one DOES need khomelab -- and
        # being listed in /etc/group is not the same as the manager having the group.
        # The manager takes its supplementary groups when it starts and, under
        # `enable-linger`, outlives every login; a group added afterwards does not reach
        # it until it restarts.
        #
        # So do not read /etc/group and infer. Ask the manager to try the read itself:
        # this is the exact operation the unit will perform.
        if ! systemd-run --user --quiet --pipe --wait \
                 test -r "$SECRETS_FILE" >/dev/null 2>&1; then
            echo "your systemd --user manager cannot read $SECRETS_FILE." >&2
            if id -nG "$(id -un)" | tr ' ' '\n' | grep -qx khomelab; then
                echo "You ARE in group khomelab in /etc/group, but the running user manager" >&2
                echo "started before that and still has its old credentials. It needs to restart:" >&2
                echo "  reboot, or 'loginctl terminate-user $(id -un)' (ends ALL your sessions" >&2
                echo "  on this host, including any agent or tmux running under them)." >&2
            else
                echo "You are not in group khomelab. Membership is declared in k-homelab's" >&2
                echo "per-host manifest under secrets_group_members -- add $(id -un) there and re-apply." >&2
            fi
            return 1
        fi
        ;;
    esac
    return 0
}

check_secrets_readable || exit 1

unit_file=$(mktemp)
trap 'rm -f "$unit_file"' EXIT

if [ "$MODE" = system ]; then
    if [ ! -x "$venv_bin/kpidash-client" ]; then
        echo "no venv found at $venv_bin -- run 'uv sync' in $client_dir first" >&2
        exit 1
    fi
    sed -e "s|__KPIDASH_CLIENT_USER__|$(id -un)|" \
        -e "s|__KPIDASH_CLIENT_VENV_BIN__|$venv_bin|" \
        "$script_dir/kpidash-client.service.template" > "$unit_file"
    sudo install -m 644 "$unit_file" /etc/systemd/system/kpidash-client.service
    sudo systemctl daemon-reload
    sudo systemctl enable --now kpidash-client.service
    echo "Installed (system). Check status with:"
    echo "  systemctl status kpidash-client.service"
    echo "  journalctl -u kpidash-client.service -f"
else
    exec_path="$HOME/.local/bin/kpidash-client"
    if [ ! -x "$exec_path" ]; then
        echo "no kpidash-client at $exec_path -- install the published package first" >&2
        echo "(kpkg install kpidash-client), or use --system to run from this checkout's venv" >&2
        exit 1
    fi
    sed -e "s|__KPIDASH_CLIENT_EXEC__|$exec_path|" \
        "$script_dir/kpidash-client.user.service.template" > "$unit_file"
    install -D -m 644 "$unit_file" "$HOME/.config/systemd/user/kpidash-client.service"
    systemctl --user daemon-reload
    systemctl --user enable --now kpidash-client.service
    echo "Installed (user). Check status with:"
    echo "  systemctl --user status kpidash-client.service"
    echo "  journalctl --user -u kpidash-client.service -f"
fi
