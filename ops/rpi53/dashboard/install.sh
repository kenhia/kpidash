#!/bin/bash
# Installs/updates the kpidash dashboard unit on rpi53. Run with sudo from the
# directory this script lives in (ops/rpi53/dashboard/ in the repo, or wherever
# it's been copied to on the target machine).
#
# This installs the UNIT and its non-secret config only. The dashboard binary is
# deployed separately by scripts/deploy.sh, which stages, installs atomically and
# restarts this service.
set -euo pipefail

if [ "$(id -u)" -ne 0 ]; then
    echo "must run as root (sudo $0)" >&2
    exit 1
fi

script_dir=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
SECRETS_FILE=/etc/khomelab/secrets.env

# The unit has no `-` on its secrets EnvironmentFile, so a missing or key-less
# file means the dashboard will not start. Say so here rather than after the
# panel goes black.
if [ ! -f "$SECRETS_FILE" ]; then
    echo "$SECRETS_FILE does not exist on this host." >&2
    echo "It is rendered by k-homelab's khomelab-secrets recipe -- apply that first." >&2
    exit 1
fi
if ! grep -q '^[[:space:]]*REDISCLI_AUTH=' "$SECRETS_FILE"; then
    echo "$SECRETS_FILE has no REDISCLI_AUTH -- this host's k-homelab manifest does not grant it." >&2
    exit 1
fi

install -d -m 755 /etc/kpidash
install -m 644 "$script_dir/config.env" /etc/kpidash/config.env
install -m 644 "$script_dir/kpidash.service" /etc/systemd/system/kpidash.service

systemctl daemon-reload
systemctl restart kpidash.service

echo "Installed. Check status with:"
echo "  systemctl status kpidash.service"
echo "  systemctl show -p EnvironmentFiles kpidash.service"
