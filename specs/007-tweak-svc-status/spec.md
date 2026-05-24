# Sprint 007 — Service Status Card Tweaks

**Branch**: `007-tweak-svc-status`
**Status**: Draft

## Problem

Two issues observed in sprint 006:

1. **Host name not displayed** on the Service Status Card even when `--host` is passed. (Already addressed as a rolled-in fix on this branch: card now has a `host_label`.)
2. **Same service name on two hosts collides**. Redis key is `kpidash:services:<name>`, so a second `service-status` publish overwrites the first regardless of `--host`. Only one card ever appears.

## Goal

Make Service Status Cards uniquely keyed by `(name, host)` so multiple hosts can publish under the same logical service name and each gets its own card. Provide an explicit "homelab" sentinel for services that are intentionally not host-scoped.

## Design decisions

- **Redis key schema**: `kpidash:services:<name>:<host>` (always includes a host segment).
- **Client default**: `--host` defaults to the sentinel string `_` (single underscore). Not a valid POSIX hostname, safe inside a Redis key, easy to type.
- **Card display for sentinel**: when `host == "_"`, the card's host line is hidden entirely (same visual as the pre-sprint-006 behaviour when no `--host` was passed). The sentinel exists purely to give every key a four-segment shape; it has no display meaning.
- **Registry**: `service_registry_find_or_create(const char *name, const char *host)` — composite key. Entry's `name` and `host` together identify it.
- **Card sort order**: alphabetical by `name`, then by `host` (so `klams/_` before `klams/kubs0` before `klams/rpi53`).
- **Migration**: no in-place migration. Old `kpidash:services:<name>` keys (no host segment) will not match the new SCAN match pattern `kpidash:services:*:*` and are silently ignored. Document one-shot cleanup: `redis-cli --scan --pattern 'kpidash:services:*' | xargs redis-cli DEL` followed by republishing.

## Functional requirements

- **FR-001**: Client `service-status` subcommand: `--host` is optional, defaults to `_`.
- **FR-002**: Redis key MUST be `kpidash:services:<name>:<host>`. Old single-segment keys are ignored on read.
- **FR-003**: Registry stores entries keyed by `(name, host)`. Two publishes with the same name and different hosts produce two registry entries and two cards.
- **FR-004**: Card host line is shown when `host` is a real value and hidden when `host == "_"` (or empty, defensive).
- **FR-005**: Cards sort by `(name, host)` ascending. Pre-existing left-to-right footer flow preserved.
- **FR-006**: JSON payload `host` field SHOULD match the key's host segment; on mismatch, the key's segment wins (it's authoritative).

## Acceptance criteria

- **AC-1**: `kpidash-client service-status --name kTest --state ok --text "..." --host kubs0` and `... --host kai` produces two cards on the dashboard, both labelled `kTest`, distinguished by `kubs0` vs `kai` host lines. (Resolves the reported bug.)
- **AC-2**: `kpidash-client service-status --name klams --state ok --text "..."` (no `--host`) publishes to `kpidash:services:klams:_` and renders a card with NO host line.
- **AC-3**: `redis-cli KEYS 'kpidash:services:*'` shows all keys have exactly four colon-separated segments.
- **AC-4**: Old single-segment keys (`kpidash:services:legacyTest`) are silently ignored — no crash, no card.
- **AC-5**: All existing sprint 006 tests still pass; new tests for the composite-key registry and the sentinel-default client pass.

## Out of scope

- Card eviction (still no path to remove cards from a running dashboard — restart required, as noted in sprint 006 follow-ups).
- Per-host filtering / muting controls.
- Authorization on who can publish under which name.
