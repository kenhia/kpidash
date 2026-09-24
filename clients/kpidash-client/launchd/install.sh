#!/bin/bash
# Installs kpidash-client as a per-user LaunchAgent on macOS (WI #3132) -- the
# counterpart of systemd/install.sh, following the pattern kmon set for the fleet's
# Macs (kmon sprint 39, tools/satellite-launchd.sh).
#
#   install    render the plist into ~/Library/LaunchAgents, lint it, and load it into
#              gui/$(id -u). Re-running is safe: an unchanged plist is left loaded; a
#              changed one is booted out and bootstrapped again.
#   status     `launchctl print gui/<uid>/net.kenhia.kpidash.client` -- rc 0 loaded,
#              rc 113 not there. This is also what k-homelab asserts.
#   uninstall  bootout and remove the plist. The log is left alone.
#   render     print the filled-in plist and install nothing. Works on any host, which
#              is how a Mac with no checkout of this repo gets one (see README.md) and
#              how the tests read it.
#
# Options:
#   --exec PATH   the kpidash-client to run. Default: the published package,
#                 ~/.local/bin/kpidash-client (a uv tool, so uv's Python -- never
#                 Apple's /usr/bin/python3, which is 3.9), else this checkout's venv.
#   --home DIR    render only: the target's home. launchd expands neither `~` nor
#                 $HOME, so every path in the plist is absolute.
#
# The Redis password is NOT written here. The plist's launcher reads it from
# /etc/khomelab/secrets.env each time launchd starts the job.
#
# /bin/bash on a Mac is 3.2: nothing here needs anything newer.
set -euo pipefail

LABEL=net.kenhia.kpidash.client
SECRETS_FILE=/etc/khomelab/secrets.env

script_dir=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
client_dir=$(cd "$script_dir/.." && pwd)
template="$script_dir/$LABEL.plist"

usage() { sed -n '2,26p' "$0"; }

action="${1:-}"
[ $# -gt 0 ] && shift
exec_override=
home_override=
while [ $# -gt 0 ]; do
    case "$1" in
        --exec) exec_override="${2:?--exec needs a path}"; shift 2 ;;
        --home) home_override="${2:?--home needs a directory}"; shift 2 ;;
        -h|--help) usage; exit 0 ;;
        *) echo "unknown arg: $1 (try --help)" >&2; exit 1 ;;
    esac
done

case "$action" in
    install|status|uninstall|render) ;;
    -h|--help) usage; exit 0 ;;
    *) echo "usage: $0 install|status|uninstall|render [--exec PATH] [--home DIR]" >&2; exit 1 ;;
esac

if [ "$action" != render ] && [ "$(uname -s)" != Darwin ]; then
    echo "$action is macOS-only: this installs a LaunchAgent. On Linux use systemd/install.sh." >&2
    exit 1
fi
if [ "$action" != render ] && [ -n "$home_override" ]; then
    echo "--home is for render only; install targets this user's home" >&2
    exit 1
fi

home="${home_override:-$HOME}"
plist_dest="$HOME/Library/LaunchAgents/$LABEL.plist"
log_dir="$home/Library/Logs/kpidash"

resolve_exec() {
    if [ -n "$exec_override" ]; then
        printf '%s' "$exec_override"
    elif [ "$action" = render ] && [ -n "$home_override" ]; then
        # Rendering for another host: this one's filesystem says nothing about it.
        printf '%s' "$home/.local/bin/kpidash-client"
    elif [ -x "$home/.local/bin/kpidash-client" ]; then
        printf '%s' "$home/.local/bin/kpidash-client"
    elif [ -x "$client_dir/.venv/bin/kpidash-client" ]; then
        printf '%s' "$client_dir/.venv/bin/kpidash-client"
    elif [ "$action" = render ]; then
        printf '%s' "$home/.local/bin/kpidash-client"
    else
        echo "no kpidash-client found." >&2
        echo "  published package: $home/.local/bin/kpidash-client" >&2
        echo "  checkout venv:     $client_dir/.venv/bin/kpidash-client (uv sync in $client_dir)" >&2
        return 1
    fi
}

render() {
    local exec_path
    exec_path=$(resolve_exec)
    sed -e "s|@HOME@|$home|g" -e "s|@EXEC@|$exec_path|g" "$template"
}

domain="gui/$(id -u)"

loaded() { launchctl print "$domain/$LABEL" >/dev/null 2>&1; }

# `launchctl bootout` returns before the job is gone: it sends SIGTERM, and the
# daemon joins its threads (up to 5 s each) before exiting. Measured on kimac,
# `print` still answered rc 0 with the process alive straight after bootout. A
# bootstrap into that window fails, so every bootout here waits for rc 113.
bootout_and_wait() {
    launchctl bootout "$domain/$LABEL" || true
    local i=0
    while loaded; do
        i=$((i + 1))
        if [ "$i" -gt 30 ]; then
            echo "$LABEL still loaded 30 s after bootout -- not continuing" >&2
            return 1
        fi
        sleep 1
    done
}

case "$action" in
render)
    render
    ;;

status)
    launchctl print "$domain/$LABEL"
    ;;

uninstall)
    if loaded; then
        bootout_and_wait
        echo "booted out $domain/$LABEL"
    else
        echo "$LABEL was not loaded"
    fi
    rm -f "$plist_dest"
    echo "removed $plist_dest (log left in $log_dir)"
    ;;

install)
    if [ ! -f "$HOME/.config/kpidash-client/config.toml" ]; then
        echo "no config at ~/.config/kpidash-client/config.toml -- create one before installing" >&2
        exit 1
    fi
    # Checked here, as this user, because the launcher's refusal is a job that
    # respawns every 10 s writing one line to a log nobody is reading. Only
    # presence is checked; the value is never read into this shell.
    if [ ! -r "$SECRETS_FILE" ]; then
        echo "$SECRETS_FILE is missing or unreadable by $(id -un)." >&2
        echo "k-homelab's khomelab-secrets recipe renders it and adds you to khomelab." >&2
        exit 1
    fi
    if ! grep -q '^[[:space:]]*REDISCLI_AUTH=' "$SECRETS_FILE"; then
        echo "$SECRETS_FILE has no REDISCLI_AUTH -- this host's k-homelab manifest does not grant it." >&2
        exit 1
    fi

    exec_path=$(resolve_exec)
    rendered=$(mktemp)
    trap 'rm -f "$rendered"' EXIT
    render > "$rendered"
    plutil -lint "$rendered" >/dev/null

    mkdir -p "$log_dir" "$(dirname "$plist_dest")"
    if [ -f "$plist_dest" ] && cmp -s "$rendered" "$plist_dest" && loaded; then
        echo "$LABEL already loaded and unchanged ($exec_path)"
        exit 0
    fi
    if loaded; then
        bootout_and_wait
    fi
    install -m 644 "$rendered" "$plist_dest"
    launchctl bootstrap "$domain" "$plist_dest"
    echo "Installed $LABEL in $domain (exec $exec_path). Check with:"
    echo "  $0 status"
    echo "  tail -f $log_dir/client.log"
    ;;
esac
