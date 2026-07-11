#!/bin/bash
# Installs kpidash-client as a systemd service on this machine, so it starts on boot and
# restarts automatically on crash -- rather than the manual `daemon start` double-fork
# mode (cli.py's `daemon_start()`), which doesn't survive a reboot or process death.
#
# Run as the user that owns this kpidash-client checkout (NOT root -- sudo is used
# internally for the parts that need it). Locates the checkout and venv relative to this
# script's own location, so it works regardless of where the repo was cloned on a given
# host (observed to differ across machines, e.g. ~/src/tools/kpidash vs ~/src/kpidash).
set -euo pipefail

if [ "$(id -u)" -eq 0 ]; then
    echo "run this as the user who owns the kpidash-client checkout, not root" >&2
    exit 1
fi

script_dir=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
client_dir=$(cd "$script_dir/.." && pwd)
venv_bin="$client_dir/.venv/bin"

if [ ! -x "$venv_bin/kpidash-client" ]; then
    echo "no venv found at $venv_bin -- run 'uv sync' in $client_dir first" >&2
    exit 1
fi

if [ ! -f "$HOME/.config/kpidash-client/config.toml" ]; then
    echo "no config at ~/.config/kpidash-client/config.toml -- create one before installing" >&2
    exit 1
fi

# systemd services start with a clean environment -- they don't inherit REDISCLI_AUTH from
# ~/.bashrc the way an interactive shell does, so it has to be supplied via EnvironmentFile
# instead. If it's already set in this (interactive) shell, use it to bootstrap that file;
# otherwise the caller needs to create it manually.
env_file="$HOME/.config/kpidash-client/redis-auth.env"
if [ ! -f "$env_file" ]; then
    if [ -n "${REDISCLI_AUTH:-}" ]; then
        echo "no $env_file yet -- creating it from this shell's REDISCLI_AUTH"
        printf 'REDISCLI_AUTH=%s\n' "$REDISCLI_AUTH" > "$env_file"
        chmod 600 "$env_file"
    else
        echo "no $env_file and REDISCLI_AUTH isn't set in this shell -- create the file" \
             "manually with 'REDISCLI_AUTH=<password>' (chmod 600) before installing" >&2
        exit 1
    fi
fi
chmod 600 "$env_file"

unit_file=$(mktemp)
sed -e "s|__KPIDASH_CLIENT_USER__|$(id -un)|" \
    -e "s|__KPIDASH_CLIENT_VENV_BIN__|$venv_bin|" \
    -e "s|__KPIDASH_CLIENT_ENV_FILE__|$env_file|" \
    "$script_dir/kpidash-client.service.template" > "$unit_file"

sudo install -m 644 "$unit_file" /etc/systemd/system/kpidash-client.service
rm -f "$unit_file"

sudo systemctl daemon-reload
sudo systemctl enable --now kpidash-client.service

echo "Installed. Check status with:"
echo "  systemctl status kpidash-client.service"
echo "  journalctl -u kpidash-client.service -f"
