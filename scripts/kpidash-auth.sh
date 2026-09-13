# kpidash-auth.sh — resolve REDISCLI_AUTH the way the whole fleet does.
#
# Sourced, not executed. Defines kpidash_load_auth(), which exports
# REDISCLI_AUTH (or returns non-zero and explains why it could not).
#
# This is the shell copy of CD-19, the fleet contract kdashdata's auth.py and
# auth.rs implement for the tools that open the per-host secrets file
# themselves. The Python client does NOT: it reads $REDISCLI_AUTH and nothing
# else, because systemd hands it the value through EnvironmentFile=. These
# shell helpers have no unit to do that for them, so they are this repo's only
# CD-19 consumers — keep them in step with kdashdata rather than inventing a
# second answer to "where is the password".
#
# Order:
#   1. $REDISCLI_AUTH already in the environment          -> use it
#   2. $KPIDASH_AUTH_FILE, if set                         -> EXCLUSIVE
#   3. /etc/khomelab/secrets.env  (shared, root:khomelab 0640)
#   4. ~/.config/kpidash-client/redis-auth.env  (deprecated, per-user 0600)
#
# Mode policy belongs to the candidate's SOURCE, not its position:
#   shared  — group read is the access mechanism, so it is allowed; group
#             WRITE and any world bit are refused (mode & 0027), because any
#             khomelab member could otherwise change the password every host
#             reads. 0640 passes; 0644 and 0660 do not.
#   private — any group or other bit is refused (mode & 0077).
#
# On the SHARED rung only, two outcomes mean "keep looking" rather than "stop":
# EACCES (this account is not in khomelab yet) and a file with no
# REDISCLI_AUTH= line (the host's manifest grants a different key set). Both
# are states of a mid-changeover fleet, not faults. A file whose mode cannot be
# trusted is fatal on EVERY rung — silently using a world-readable fleet
# password is worse than failing loudly.

KPIDASH_SECRETS_FILE="${KPIDASH_SECRETS_FILE:-/etc/khomelab/secrets.env}"

# Extract one key's value. Handles k-homelab's KEY='value' single quoting and
# bare KEY=value alike. Never evals, so a value containing $ or ` is safe.
_kpa_extract() {
    sed -n "s/^[[:space:]]*$2=//p" "$1" 2>/dev/null | head -n1 | sed -e "s/^'\(.*\)'$/\1/" -e 's/^"\(.*\)"$/\1/'
}

# Refuse a file whose mode grants more than this source should. Returns 0 when
# the mode is acceptable, 1 when it is not.
_kpa_mode_ok() {
    _m=$(stat -c '%a' "$1" 2>/dev/null) || return 0   # cannot stat: not a mode failure
    [ -n "$_m" ] || return 0
    [ $(( 8#$_m & $2 )) -eq 0 ]
}

kpidash_load_auth() {
    # 1. Already in the environment — systemd, a parent shell, or an explicit export.
    if [ -n "${REDISCLI_AUTH:-}" ]; then
        KPIDASH_AUTH_SOURCE="environment"
        return 0
    fi

    # 2. An explicit file is exclusive: if it is named, it is the answer or the error.
    if [ -n "${KPIDASH_AUTH_FILE:-}" ]; then
        if [ ! -f "$KPIDASH_AUTH_FILE" ]; then
            echo "kpidash-auth: KPIDASH_AUTH_FILE=$KPIDASH_AUTH_FILE does not exist" >&2
            return 1
        fi
        if ! _kpa_mode_ok "$KPIDASH_AUTH_FILE" 027; then
            echo "kpidash-auth: $KPIDASH_AUTH_FILE is group-writable or world-readable — refusing" >&2
            return 1
        fi
        _v=$(_kpa_extract "$KPIDASH_AUTH_FILE" REDISCLI_AUTH)
        if [ -z "$_v" ]; then
            echo "kpidash-auth: $KPIDASH_AUTH_FILE has no REDISCLI_AUTH" >&2
            return 1
        fi
        REDISCLI_AUTH="$_v"; export REDISCLI_AUTH
        KPIDASH_AUTH_SOURCE="KPIDASH_AUTH_FILE ($KPIDASH_AUTH_FILE)"
        return 0
    fi

    # 3. The per-host file every tool we own reads.
    if [ -f "$KPIDASH_SECRETS_FILE" ]; then
        if ! _kpa_mode_ok "$KPIDASH_SECRETS_FILE" 027; then
            echo "kpidash-auth: $KPIDASH_SECRETS_FILE is group-writable or world-readable — refusing" >&2
            return 1
        fi
        if [ -r "$KPIDASH_SECRETS_FILE" ]; then
            _v=$(_kpa_extract "$KPIDASH_SECRETS_FILE" REDISCLI_AUTH)
            if [ -n "$_v" ]; then
                REDISCLI_AUTH="$_v"; export REDISCLI_AUTH
                KPIDASH_AUTH_SOURCE="per-host secrets file ($KPIDASH_SECRETS_FILE)"
                return 0
            fi
            # No REDISCLI_AUTH line: this host's manifest grants other keys. Keep looking.
        fi
        # Unreadable: this account is not in khomelab yet. Keep looking.
    fi

    # 4. The deprecated per-user copy, until the changeover deletes it.
    _dep="$HOME/.config/kpidash-client/redis-auth.env"
    if [ -f "$_dep" ]; then
        if ! _kpa_mode_ok "$_dep" 077; then
            echo "kpidash-auth: $_dep is group- or world-readable — refusing" >&2
            return 1
        fi
        _v=$(_kpa_extract "$_dep" REDISCLI_AUTH)
        if [ -n "$_v" ]; then
            REDISCLI_AUTH="$_v"; export REDISCLI_AUTH
            KPIDASH_AUTH_SOURCE="deprecated per-user file ($_dep)"
            echo "kpidash-auth: using $_dep — deprecated; $KPIDASH_SECRETS_FILE is where this lives now" >&2
            return 0
        fi
    fi

    echo "kpidash-auth: no REDISCLI_AUTH found (tried \$REDISCLI_AUTH, $KPIDASH_SECRETS_FILE, $_dep)" >&2
    return 1
}
