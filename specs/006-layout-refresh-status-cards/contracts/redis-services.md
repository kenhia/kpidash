# Redis Contract — Service Status (`kpidash:services:<name>`)

**Sprint**: 006
**Key family**: `kpidash:services:<name>`
**Type**: STRING (JSON value)
**Written by**: any service publisher (e.g. `kpidash-client service-status …`)
**Read by**: kpidash dashboard
**TTL**: **none** — the dashboard computes freshness itself from the payload's `ts`. Publishers MUST NOT set a TTL.

---

## Key naming

```
kpidash:services:<name>
```

- `<name>` is the service identifier. Lowercase recommended; characters MUST be safe in a Redis key (avoid `:` inside `<name>`).
- One key per service. Republishing overwrites the previous value (atomic `SET`).
- Discovery is performed dashboard-side via `SCAN MATCH kpidash:services:*`.

## Value (JSON)

```json
{
  "ts": 1748000000.123,
  "state": "ok",
  "text": "ingest queue 0",
  "host": "kai",
  "icon": 7
}
```

| Field   | Type    | Required | Description |
|---------|---------|----------|-------------|
| `ts`    | number  | yes      | Unix epoch seconds (float allowed). The dashboard's 60 s freshness check compares this to wall-clock. |
| `state` | string  | yes      | One of `ok` \| `unhealthy` \| `maintenance` \| `down`. |
| `text`  | string  | yes      | Short human-readable status; rendered on the card. |
| `host`  | string  | no       | Optional host annotation. |
| `icon`  | integer | no       | Index into the dashboard's icon registry. Unknown indices render no icon. |

## Writer rules

- Use `SET kpidash:services:<name> <json>` — no `EX`, no `PX`.
- `ts` SHOULD be the publisher's wall-clock at the moment of publish.
- Publishers SHOULD send a full payload on every update (no partial updates).

## Reader rules (dashboard)

- On each poll cycle:
  1. `SCAN MATCH kpidash:services:*` to enumerate keys.
  2. `GET` each value (or use `MGET` for a batch).
  3. Parse JSON. If the payload is missing `state`, or `state` is not in the enum, OR `ts` is missing/non-numeric, OR `text` is missing — the payload is **malformed**: ignore it and DO NOT update the in-memory entry (FR-022a). MAY log the malformed payload for debugging.
  4. Otherwise, update the service entry's `last_valid_state`, `last_payload_ts`, `text`, `host`, `icon`.
- Border colour is computed from `last_valid_state` and `(now - last_payload_ts < 60.0)` per `data-model.md` §2.
- A service entry, once created, persists in-session (no eviction). Restart re-discovers services via `SCAN`.

## Semantics

- **No TTL** — required by FR-020 (the dashboard, not Redis, owns freshness).
- **No companion set** — `SCAN MATCH` is the source of truth for which services exist.
- **No retention beyond "latest"** — only the most recent value matters. Historical state is not kept in Redis.

## Operator usage examples

```bash
# Publish OK
redis-cli SET kpidash:services:ingest '{"ts":1748000000.0,"state":"ok","text":"queue 0"}'

# Publish maintenance
redis-cli SET kpidash:services:ingest \
  '{"ts":1748000060.0,"state":"maintenance","text":"deploy in progress","host":"kai","icon":7}'

# List all services
redis-cli --scan --pattern 'kpidash:services:*'
```

## Backwards compatibility

This is a new key family introduced in sprint 006. No legacy schema to migrate. Publishers from before sprint 006 do not exist for this key.
