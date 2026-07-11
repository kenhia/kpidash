#!/usr/bin/env bash
#
# deploy.sh — safe, idempotent deploy of the kpidash dashboard to a Pi.
#
# Replaces the fragile `~/kpidash.new && mv` pattern (which could silently ship
# an OLD binary when the scp failed on a root-owned target). This script stages
# through /tmp, installs atomically with `install(1)`, keeps a single rolling
# backup for one-step rollback, restarts the service, and verifies health —
# including reading back the version the running binary self-reports.
#
# Usage:
#   scripts/deploy.sh [--host HOST] [--no-build] [--rollback]
#
#   --host HOST   target host (default: rpi53); user is always `ken`
#   --no-build    skip the cross-build, deploy the existing build-pi/kpidash
#   --rollback    restore the previous binary (~/kpidash.prev) and restart
#
set -euo pipefail

HOST=rpi53
REMOTE_USER=ken
SERVICE=kpidash
DO_BUILD=1
ROLLBACK=0

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="$REPO_ROOT/build-pi"
TOOLCHAIN="$REPO_ROOT/cmake/aarch64-toolchain.cmake"
REMOTE_BIN="/home/$REMOTE_USER/kpidash"
REMOTE_PREV="/home/$REMOTE_USER/kpidash.prev"

die() { echo "deploy: $*" >&2; exit 1; }
say() { echo ">> $*"; }

while [ $# -gt 0 ]; do
    case "$1" in
        --host) HOST="${2:?--host needs a value}"; shift 2 ;;
        --no-build) DO_BUILD=0; shift ;;
        --rollback) ROLLBACK=1; shift ;;
        -h|--help) sed -n '3,17p' "$0"; exit 0 ;;
        *) die "unknown arg: $1 (try --help)" ;;
    esac
done

REMOTE="$REMOTE_USER@$HOST"

# ---- health check helper: service active with no crash loop --------------
health_check() {
    sleep 3
    local active restarts
    active=$(ssh "$REMOTE" "systemctl is-active $SERVICE" || true)
    restarts=$(ssh "$REMOTE" "systemctl show -p NRestarts --value $SERVICE" 2>/dev/null || echo "?")
    [ "$active" = "active" ] || die "service is '$active' after deploy (NRestarts=$restarts) — check 'journalctl -u $SERVICE', or re-run with --rollback"
    say "service active (NRestarts=$restarts)"
    # Best-effort: read back the version the running binary published to Redis.
    # Redis requires auth; source the ken-readable client auth env if present.
    local ver
    ver=$(ssh "$REMOTE" 'f="$HOME/.config/kpidash-client/redis-auth.env"; [ -f "$f" ] && { set -a; . "$f"; set +a; }; redis-cli GET kpidash:system:version 2>/dev/null' || true)
    [ -n "$ver" ] && say "dashboard reports version: $ver" || say "(could not read kpidash:system:version — non-fatal)"
}

# ---- rollback path -------------------------------------------------------
if [ "$ROLLBACK" = 1 ]; then
    ssh "$REMOTE" "test -f '$REMOTE_PREV'" || die "no previous binary at $REMOTE:$REMOTE_PREV"
    say "rolling back $REMOTE:$REMOTE_BIN <- kpidash.prev"
    ssh "$REMOTE" "sudo install -m755 -o $REMOTE_USER -g $REMOTE_USER '$REMOTE_PREV' '$REMOTE_BIN' && sudo systemctl restart $SERVICE"
    health_check
    say "rollback complete"
    exit 0
fi

# ---- build ---------------------------------------------------------------
if [ "$DO_BUILD" = 1 ]; then
    if [ ! -f "$BUILD_DIR/CMakeCache.txt" ]; then
        say "configuring cross-build in $BUILD_DIR"
        cmake -S "$REPO_ROOT" -B "$BUILD_DIR" \
            -DCMAKE_TOOLCHAIN_FILE="$TOOLCHAIN" -DCMAKE_BUILD_TYPE=Release
    fi
    say "building kpidash (aarch64)"
    cmake --build "$BUILD_DIR" --target kpidash -j"$(nproc)"
fi

BIN="$BUILD_DIR/kpidash"
[ -f "$BIN" ] || die "binary not found: $BIN (drop --no-build?)"
file "$BIN" | grep -q 'ARM aarch64' || die "$BIN is not an aarch64 binary"

VER=$(sed -n 's/.*KPIDASH_BUILD_VERSION "\(.*\)".*/\1/p' "$BUILD_DIR/generated/kpidash_version.h" 2>/dev/null || echo "unknown")

# ---- stage + install + restart ------------------------------------------
STAGE="/tmp/kpidash.$$.$(date +%s)"
say "deploying [$VER] to $REMOTE"
scp -q "$BIN" "$REMOTE:$STAGE"
ssh "$REMOTE" bash -s -- "$STAGE" "$REMOTE_BIN" "$REMOTE_PREV" "$REMOTE_USER" "$SERVICE" <<'REMOTE_EOF'
set -euo pipefail
stage="$1"; bin="$2"; prev="$3"; user="$4"; svc="$5"
# keep a single rolling backup for one-step rollback
[ -f "$bin" ] && sudo cp -a "$bin" "$prev"
sudo install -m755 -o "$user" -g "$user" "$stage" "$bin"
rm -f "$stage"
sudo systemctl restart "$svc"
REMOTE_EOF

health_check
say "deploy complete"
