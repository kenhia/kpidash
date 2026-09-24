#!/usr/bin/env bash
# unit-lint — assert every systemd unit (and, since sprint 023, the macOS
# LaunchAgent) this repo authors reads the Redis password from the one per-host
# file, and from nowhere else.
#
# Why this is a gate and not a comment: a unit that reads a private copy of the
# password keeps rendering live data from a credential nobody maintains, and
# goes on doing it until the copy drifts. The failure is silent for as long as
# the two values happen to agree -- which, on the day of the changeover, they
# did. Bytes in the repo are the only place this can be checked cheaply.
set -uo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SECRETS_FILE=/etc/khomelab/secrets.env

UNITS=(
    "clients/kpidash-client/systemd/kpidash-client.service.template"
    "ops/rpi53/dashboard/kpidash.service"
)

# Files whose presence in a unit means a private copy of the fleet password.
RETIRED=(
    "redis-auth.env"
    "/etc/kpidash.env"
)

fail=0
note() { printf '  %s\n' "$1"; }
err()  { fail=1; printf '  FAIL %s\n       %s\n' "$1" "$2"; }

echo "unit-lint: every unit reads $SECRETS_FILE and no private copy"

for u in "${UNITS[@]}"; do
    f="$REPO_ROOT/$u"
    if [ ! -f "$f" ]; then
        err "$u" "missing -- a unit this repo is supposed to author is gone"
        continue
    fi

    if ! grep -q "^EnvironmentFile=$SECRETS_FILE\$" "$f"; then
        err "$u" "does not read $SECRETS_FILE"
        continue
    fi

    bad=""
    for r in "${RETIRED[@]}"; do
        grep -q -- "$r" "$f" && bad="$bad $r"
    done
    if [ -n "$bad" ]; then
        err "$u" "still references a retired private password file:$bad"
        continue
    fi

    # kstudiodash measured this on systemd 259 and it cost a sprint to find:
    # EnvironmentFile= is read by PID 1 before User= is applied, so a system
    # unit needs no membership; and a SupplementaryGroups= naming a group that
    # does not exist stops the unit starting at all, which turns an optional
    # EnvironmentFile into a hard dependency on k-homelab having converged.
    if grep -q '^SupplementaryGroups=.*khomelab' "$f"; then
        err "$u" "declares SupplementaryGroups=khomelab -- see the comment in this linter"
        continue
    fi

    note "ok   $u"
done

# The macOS LaunchAgent (WI #3132). launchd has no EnvironmentFile=, so the plist's
# launcher script reads the same file itself -- the check is the same question in
# the plist's own terms: it names the one per-host file as the launcher's argument,
# names no private copy, and carries no EnvironmentVariables dict, which is the one
# place in a plist a password could be pasted as a literal.
PLISTS=(
    "clients/kpidash-client/launchd/net.kenhia.kpidash.client.plist"
)
for u in "${PLISTS[@]}"; do
    f="$REPO_ROOT/$u"
    if [ ! -f "$f" ]; then
        err "$u" "missing -- a LaunchAgent this repo is supposed to author is gone"
        continue
    fi
    if ! grep -q "<string>$SECRETS_FILE</string>" "$f"; then
        err "$u" "does not hand its launcher $SECRETS_FILE"
        continue
    fi
    bad=""
    for r in "${RETIRED[@]}"; do
        grep -q -- "$r" "$f" && bad="$bad $r"
    done
    if [ -n "$bad" ]; then
        err "$u" "still references a retired private password file:$bad"
        continue
    fi
    if grep -q '<key>EnvironmentVariables</key>' "$f"; then
        err "$u" "declares EnvironmentVariables -- the password comes from $SECRETS_FILE only"
        continue
    fi
    note "ok   $u"
done

# Sprint 021 retired the `systemd --user` shape outright: such a unit reads
# EnvironmentFile= as the invoking user, and a lingering manager keeps the
# groups it started with, so it can never read the root:khomelab file. A
# template for that shape is a template for a unit that cannot work -- and the
# way it comes back is somebody copying the system one "for kai".
while IFS= read -r stray; do
    err "${stray#"$REPO_ROOT/"}" "user-unit template -- the --user shape was retired in sprint 021"
done < <(find "$REPO_ROOT" -name '*.user.service.template' -not -path '*/.venv/*' 2>/dev/null)

if [ "$fail" -ne 0 ]; then
    echo
    echo "unit-lint: FAILED" >&2
    exit 1
fi
echo "unit-lint: ok"
