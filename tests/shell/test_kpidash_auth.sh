#!/usr/bin/env bash
# Tests for scripts/kpidash-auth.sh — this repo's shell copy of CD-19.
#
# Each case runs in a subshell with a fresh $HOME and a fresh fake per-host
# file, because the helper reads both. No real secret appears here: the
# "passwords" are literals chosen to be obviously fake.
set -uo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
HELPER="$REPO_ROOT/scripts/kpidash-auth.sh"

pass=0; fail=0
ok()  { pass=$((pass+1)); printf '  ok   %s\n' "$1"; }
bad() { fail=$((fail+1)); printf '  FAIL %s\n       %s\n' "$1" "$2"; }

# Run kpidash_load_auth in isolation; print "<rc>|<value>|<source-kind>".
# The source is reduced to its first word so tests do not depend on temp paths.
run_case() {
    (
        SB=$(mktemp -d); export SB
        HOME="$SB/home"; mkdir -p "$HOME/.config/kpidash-client"; export HOME
        unset REDISCLI_AUTH KPIDASH_AUTH_FILE
        KPIDASH_SECRETS_FILE="$SB/secrets.env"; export KPIDASH_SECRETS_FILE
        shared()  { printf "REDISCLI_AUTH='%s'\n" "$2" > "$1"; chmod 640 "$1"; }
        private() { printf 'REDISCLI_AUTH=%s\n' "$2" > "$1"; chmod 600 "$1"; }
        DEP="$HOME/.config/kpidash-client/redis-auth.env"
        eval "$1"
        # shellcheck disable=SC1090
        . "$HELPER"
        kpidash_load_auth 2>/dev/null
        rc=$?
        printf '%s|%s|%s' "$rc" "${REDISCLI_AUTH:-}" "${KPIDASH_AUTH_SOURCE:-}"
        rm -rf "$SB"
    )
}

expect() { # name expected-rc expected-value expected-source-prefix setup
    local name="$1" erc="$2" eval_="$3" esrc="$4" setup="$5"
    local r; r=$(run_case "$setup")
    local rc="${r%%|*}" rest="${r#*|}"
    local val="${rest%%|*}" src="${rest#*|}"
    if [ "$rc" = "$erc" ] && [ "$val" = "$eval_" ] && [ "${src#"$esrc"}" != "$src" -o -z "$esrc" ]; then
        ok "$name"
    else
        bad "$name" "expected rc=$erc value=[$eval_] source~[$esrc]; got rc=$rc value=[$val] source=[$src]"
    fi
}

echo "kpidash-auth: CD-19 order and mode policy"

# --- rung 1: the environment wins over every file -------------------------
expect "environment beats the per-host file" 0 from-env "environment" '
    REDISCLI_AUTH=from-env; export REDISCLI_AUTH
    shared "$KPIDASH_SECRETS_FILE" from-per-host'

# --- rung 2: an explicit file is exclusive --------------------------------
expect "KPIDASH_AUTH_FILE wins over the per-host file" 0 from-explicit "KPIDASH_AUTH_FILE" '
    shared "$KPIDASH_SECRETS_FILE" from-per-host
    private "$SB/explicit.env" from-explicit
    KPIDASH_AUTH_FILE="$SB/explicit.env"; export KPIDASH_AUTH_FILE'

expect "a missing KPIDASH_AUTH_FILE is fatal, not a fallthrough" 1 "" "" '
    shared "$KPIDASH_SECRETS_FILE" from-per-host
    KPIDASH_AUTH_FILE="$SB/missing.env"; export KPIDASH_AUTH_FILE'

# --- rung 3: the per-host file --------------------------------------------
expect "per-host file is used at 0640" 0 from-per-host "per-host" '
    shared "$KPIDASH_SECRETS_FILE" from-per-host'

expect "single quotes stripped and a \$ in the value survives" 0 'pa$$word' "per-host" '
    shared "$KPIDASH_SECRETS_FILE" "pa\$\$word"'

expect "0644 per-host file is FATAL (world-readable)" 1 "" "" '
    shared "$KPIDASH_SECRETS_FILE" v; chmod 644 "$KPIDASH_SECRETS_FILE"'

expect "0660 per-host file is FATAL (group-writable)" 1 "" "" '
    shared "$KPIDASH_SECRETS_FILE" v; chmod 660 "$KPIDASH_SECRETS_FILE"'

# The two outcomes that mean "keep looking" rather than "stop".
expect "unreadable per-host file falls through (EACCES = keep looking)" 0 from-per-user "deprecated" '
    shared "$KPIDASH_SECRETS_FILE" from-per-host; chmod 040 "$KPIDASH_SECRETS_FILE"
    private "$DEP" from-per-user'

expect "per-host file without a REDISCLI_AUTH line falls through" 0 from-per-user "deprecated" '
    printf "HF_TOKEN=%s\n" other > "$KPIDASH_SECRETS_FILE"; chmod 640 "$KPIDASH_SECRETS_FILE"
    private "$DEP" from-per-user'

# --- rung 4: the deprecated per-user file ---------------------------------
expect "deprecated per-user file still works" 0 from-per-user "deprecated" '
    private "$DEP" from-per-user'

expect "a group-readable per-user file is FATAL" 1 "" "" '
    private "$DEP" v; chmod 640 "$DEP"'

# --- nothing at all --------------------------------------------------------
expect "no credential anywhere returns non-zero" 1 "" "" ':'

echo
printf '%d passed, %d failed\n' "$pass" "$fail"
[ "$fail" -eq 0 ]
