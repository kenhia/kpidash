#!/bin/bash
# Installs kpidash-client as a systemd SYSTEM service on this machine, so it starts on
# boot and restarts automatically on crash -- rather than the manual `daemon start`
# double-fork mode (cli.py's `daemon_start()`), which doesn't survive a reboot or
# process death.
#
# One shape, deliberately. Until sprint 021 this script also installed a `systemd --user`
# unit, which is what kai, kubs0 and kubsdb ran. That shape cannot read the fleet's one
# copy of the Redis password: `systemd --user` is not PID 1, it reads EnvironmentFile=
# with your credentials, and a lingering manager takes its supplementary groups when it
# starts and outlives every login -- so `khomelab` membership added afterwards never
# reaches it. A system unit has no such problem, because PID 1 reads the file as root.
# `--user` is therefore refused rather than fixed; see the message it prints.
#
#   (default)      /etc/systemd/system/kpidash-client.service, User=<you>, ExecStart
#                  from the published package (~/.local/bin) or, failing that, this
#                  checkout's venv.
#   --render-only  print the rendered unit to stdout and install nothing. This is how a
#                  host with no checkout of this repo gets one: render here, deliver the
#                  bytes, install there. Target-host state is NOT checked in this mode --
#                  it is not this host's state to check.
#
# Run as the user that owns this kpidash-client checkout (NOT root -- sudo is used
# internally for the parts that need it). Locates the checkout and venv relative to this
# script's own location, so it works regardless of where the repo was cloned on a given
# host (observed to differ across machines, e.g. ~/src/tools/kpidash vs ~/src/kpidash).
#
# The Redis password is NOT written here. It lives once per host in
# /etc/khomelab/secrets.env, rendered by k-homelab from the age store, and the unit
# reads it with EnvironmentFile=.
set -euo pipefail

SECRETS_FILE=/etc/khomelab/secrets.env
RENDER_ONLY=0
EXEC_OVERRIDE=
RUN_AS=

while [ $# -gt 0 ]; do
    case "$1" in
        --system) shift ;;   # accepted and ignored: it is the only shape now
        --render-only) RENDER_ONLY=1; shift ;;
        --exec) EXEC_OVERRIDE="${2:?--exec needs a path}"; shift 2 ;;
        --run-as) RUN_AS="${2:?--run-as needs a user name}"; shift 2 ;;
        --user)
            cat >&2 <<'REFUSED'
--user is refused: the user-unit shape was retired in sprint 021.

A `systemd --user` unit cannot read /etc/khomelab/secrets.env, which is where the
fleet's one copy of the Redis password lives. `systemd --user` is not PID 1 -- it runs
as you and reads EnvironmentFile= with your credentials -- and under
`loginctl enable-linger` the manager outlives every login, so a `khomelab` membership
added after it started never reaches it. The unit either fails to start or, worse,
falls back to a private copy of the password that nobody maintains.

Install the system unit instead (no arguments). PID 1 reads the file as root before
dropping to User=, so no group membership is involved at all:

  ./systemd/install.sh

If this host has no checkout of the repo, render the unit on one that does and deliver
it -- see "Installing on a host with no checkout" in systemd/README.md.
REFUSED
            exit 1
            ;;
        -h|--help) sed -n '2,32p' "$0"; exit 0 ;;
        *) echo "unknown arg: $1 (try --help)" >&2; exit 1 ;;
    esac
done

if [ "$(id -u)" -eq 0 ]; then
    echo "run this as the user who owns the kpidash-client checkout, not root" >&2
    exit 1
fi

script_dir=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
client_dir=$(cd "$script_dir/.." && pwd)
venv_exec="$client_dir/.venv/bin/kpidash-client"
published_exec="$HOME/.local/bin/kpidash-client"

run_as="${RUN_AS:-$(id -un)}"

# --- which binary the unit runs --------------------------------------------------------
#
# kai, kubs0 and kubsdb run the PUBLISHED package (`kpkg install kpidash-client`, a uv
# tool symlinked into ~/.local/bin); rpi53 runs this checkout's venv. Prefer the
# published package: a host that has both is a dev host, and the published build is the
# one the fleet is actually running.
resolve_exec() {
    if [ -n "$EXEC_OVERRIDE" ]; then
        printf '%s' "$EXEC_OVERRIDE"
        return 0
    fi
    if [ -x "$published_exec" ]; then
        printf '%s' "$published_exec"
        return 0
    fi
    if [ -x "$venv_exec" ]; then
        printf '%s' "$venv_exec"
        return 0
    fi
    if [ "$RENDER_ONLY" -eq 1 ]; then
        # Rendering for somewhere else: this host's filesystem says nothing about the
        # target's. Fall back to the fleet's standard path and say so.
        echo "note: no kpidash-client found on THIS host; rendering for the published" >&2
        echo "      package path $published_exec. Pass --exec to override." >&2
        printf '%s' "$published_exec"
        return 0
    fi
    echo "no kpidash-client found." >&2
    echo "  published package: $published_exec (install with 'kpkg install kpidash-client')" >&2
    echo "  checkout venv:     $venv_exec (create with 'uv sync' in $client_dir)" >&2
    return 1
}

exec_path=$(resolve_exec)

render_unit() {
    sed -e "s|__KPIDASH_CLIENT_USER__|$run_as|" \
        -e "s|__KPIDASH_CLIENT_EXEC__|$exec_path|" \
        "$script_dir/kpidash-client.service.template"
}

if [ "$RENDER_ONLY" -eq 1 ]; then
    echo "rendering only: User=$run_as ExecStart=$exec_path" >&2
    echo "target-host state (secrets file, config.toml) not checked -- check it there" >&2
    render_unit
    exit 0
fi

# --- from here on we are installing on THIS host ---------------------------------------

if [ ! -f "$HOME/.config/kpidash-client/config.toml" ]; then
    echo "no config at ~/.config/kpidash-client/config.toml -- create one before installing" >&2
    exit 1
fi

# The unit has no `-` on its EnvironmentFile=, so systemd refuses to start it if the
# file cannot be read. That is deliberate (a client with no password connects and fails
# every write with NOAUTH while looking healthy), but it means a bad install is a dead
# service rather than a warning -- so check it HERE, before installing anything.
#
# systemd reads EnvironmentFile= as PID 1, as root, BEFORE dropping to User=. So the
# unit's own account needs no khomelab membership, and must not be given
# SupplementaryGroups=khomelab (a group that does not exist would stop the unit
# starting). Root can always read it; what is worth asserting is that the key this
# client needs is actually present on this host -- the per-host file holds only the keys
# that host's manifest grants.
if [ ! -f "$SECRETS_FILE" ]; then
    echo "$SECRETS_FILE does not exist on this host." >&2
    echo "It is rendered by k-homelab's khomelab-secrets recipe -- run 'bin/apply <host> khomelab-secrets'" >&2
    echo "from the k-homelab control node before installing." >&2
    exit 1
fi
if ! sudo grep -q '^[[:space:]]*REDISCLI_AUTH=' "$SECRETS_FILE"; then
    echo "$SECRETS_FILE has no REDISCLI_AUTH -- this host's k-homelab manifest does not" >&2
    echo "grant it. Add REDISCLI_AUTH to the host's secrets: key list and re-apply." >&2
    exit 1
fi

unit_file=$(mktemp)
trap 'rm -f "$unit_file"' EXIT
render_unit > "$unit_file"

sudo install -m 644 "$unit_file" /etc/systemd/system/kpidash-client.service
sudo systemctl daemon-reload
sudo systemctl enable --now kpidash-client.service

# A user unit left running would write the same Redis keys as the system unit we just
# started. Retire it here rather than leaving it to be noticed later. Linger is
# deliberately NOT touched: other things run under that manager.
if systemctl --user list-unit-files kpidash-client.service >/dev/null 2>&1 &&
   [ -f "$HOME/.config/systemd/user/kpidash-client.service" ]; then
    echo "retiring the user unit this host used to run..."
    systemctl --user disable --now kpidash-client.service || true
    rm -f "$HOME/.config/systemd/user/kpidash-client.service"
    systemctl --user daemon-reload || true
    echo "  (loginctl enable-linger left alone -- not ours to change)"
fi

echo "Installed (system, User=$run_as, ExecStart=$exec_path). Check status with:"
echo "  systemctl status kpidash-client.service"
echo "  journalctl -u kpidash-client.service -f"
