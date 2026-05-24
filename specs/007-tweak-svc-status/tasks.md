# Sprint 007 — Tasks

**Branch**: `007-tweak-svc-status`
**Spec**: [spec.md](spec.md)

Format: `- [ ] T0XX description`. Mark `[X]` on completion.

## Phase 1 — Already landed (rolled in)

- [X] T001 Add `host_label` to `service_entry_t` and render it on the card. (Pre-existing fix on this branch.)

## Phase 2 — Composite key (C side)

- [X] T002 In `src/registry.h`, change `service_registry_find_or_create(const char *name)` to `service_registry_find_or_create(const char *name, const char *host)`. Update the registry's match logic in `src/registry.c` to compare both fields.
- [X] T003 In `tests/test_service_card.c`, add a registry case: two `find_or_create("svc", "a")` / `("svc", "b")` calls return distinct pointers; a third `("svc", "a")` returns the first pointer. Must FAIL before T002 lands and PASS after.
- [X] T004 In `src/redis.c`'s `redis_poll_services`, change the SCAN match pattern to `kpidash:services:*:*` and parse the key into `name` and `host` segments. Pass both to `service_registry_find_or_create`. Skip keys that don't have exactly four colon segments (silent ignore — FR-002 / AC-4).
- [X] T005 In `src/redis.c`'s `redis_parse_service_payload`, stop trusting the JSON `host` field as authoritative — the caller (T004) supplies the host from the key segment. Keep parsing the JSON `host` only as a fallback if the key segment is empty (defensive). Update `tests/test_service_card.c` JSON-parse cases accordingly.

## Phase 3 — Card display

- [X] T006 In `src/widgets/service_card.c`, treat `e->host == "_"` the same as an empty host: hide the host label via `LV_OBJ_FLAG_HIDDEN`. Otherwise render `e->host` verbatim and clear the hidden flag. Applies in both `service_card_create` and `service_card_update`.
- [X] T007 In `src/ui.c`'s service-strip placement, sort the snapshot by `(name, host)` ascending before creating/positioning cards (FR-005). The existing sort by name alone, if any, needs to extend to include host as the secondary key.

## Phase 4 — Client default

- [X] T008 In `clients/kpidash-client/kpidash_client/cli.py`'s `service-status` subcommand, change `--host` from optional-no-default to `default="_", show_default=True`. The published Redis key becomes `kpidash:services:<name>:<host>` always.
- [X] T009 In `clients/kpidash-client/kpidash_client/redis_client.py` (or wherever the service-status writer lives), update the key construction to include the host segment.
- [X] T010 In `clients/kpidash-client/tests/test_service_status.py`, update existing test expectations to assert the new key format; add a new test that omitting `--host` writes to `kpidash:services:<name>:_`.

## Phase 5 — Docs & ship

- [X] T011 In `docs/CLIENT-PROTOCOL.md`, update the `kpidash:services:<name>` section to document the new `kpidash:services:<name>:<host>` schema, the `_` sentinel, and the migration note (one-shot `redis-cli` DEL).
- [X] T012 Build native + ctest + cross-build clean. Deploy to rpi53. Seed three test cards: one per host (kubs0, kai) under name `kTest`, and one under name `klams` with no `--host`. Confirm three cards appear with correct host lines (`kubs0`, `kai`, hidden for the no-host case).
- [ ] T013 Use the sprint-ship skill to commit, PR, merge, cleanup.
