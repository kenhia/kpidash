# Redis Protocol Contract: kpidash MVP

**Version**: 1.0 (MVP)
**Status**: Draft
**Canonical location**: This file → `docs/CLIENT-PROTOCOL.md` (once ratified)

This document is the authoritative contract for all Redis key names, value
formats, TTLs, and access patterns in the kpidash system. All components
(dashboard C process, Python client daemons, MCP server) MUST conform to
this schema.

---

## Key Naming Conventions

- All keys are prefixed `kpidash:`.
- Segment separator is `:`.
- Hostnames are lowercase, matching the system `hostname` command output.
- Field names in JSON values use `snake_case`.

---

## Keys Reference

### Client Registry

| Key | Type | Written by | Read by |
|-----|------|-----------|---------|
| `kpidash:clients` | SET | client on startup | dashboard |

**Operation**: `SADD kpidash:clients {hostname}` on each daemon start.
No TTL. Dashboard uses `SMEMBERS kpidash:clients` to enumerate clients.

---

### Health

| Key | Type | Written by | Read by | TTL |
|-----|------|-----------|---------|-----|
| `kpidash:client:{hostname}:health` | STRING (JSON) | client (~3s) | dashboard | 5 s |

**Value format**:
```json
{
  "hostname": "kubs0",
  "last_seen_ts": 1743292800.123,
  "uptime_seconds": 86400.5
}
```

`uptime_seconds` MAY be omitted. Dashboard infers offline status when this
key is absent (TTL expired) or the key does not exist.

**Write pattern**:
```
SET kpidash:client:kubs0:health '{"hostname":"kubs0","last_seen_ts":1743292800.123,"uptime_seconds":86400.5}' EX 5
```

---

### Telemetry

| Key | Type | Written by | Read by | TTL |
|-----|------|-----------|---------|-----|
| `kpidash:client:{hostname}:telemetry` | STRING (JSON) | client (~5s) | dashboard | 15 s |

**Value format**:
```json
{
  "hostname": "kubs0",
  "ts": 1743292800.456,
  "cpu_pct": 45.2,
  "top_core_pct": 78.1,
  "ram_used_mb": 6144,
  "ram_total_mb": 16384,
  "gpu": {
    "name": "NVIDIA GeForce RTX 4090",
    "vram_used_mb": 3072,
    "vram_total_mb": 24576,
    "compute_pct": 32.5
  },
  "disks": [
    {
      "label": "root",
      "type": "nvme",
      "used_gb": 237.4,
      "total_gb": 953.9,
      "pct": 24.9
    }
  ]
}
```

`gpu` MAY be `null` or omitted if no GPU. `disks` array MUST contain at least
one entry if any disks are configured; it MAY be empty if no disks are
configured.

Valid `type` values: `"nvme"`, `"ssd"`, `"hdd"`, `"other"`.

**Write pattern**:
```
SET kpidash:client:kubs0:telemetry '{...}' EX 15
```

---

### Activities

| Key | Type | Written by | Read by | TTL |
|-----|------|-----------|---------|-----|
| `kpidash:activities` | ZSET (score=start_ts, member=activity_id) | any client | dashboard | none |
| `kpidash:activity:{activity_id}` | HASH | any client | dashboard | none |

**Activity HASH fields**:

| Field | Type | Notes |
|-------|------|-------|
| `activity_id` | string (UUID4) | Redundant copy for convenience |
| `host` | string | Reporting hostname or agent name |
| `name` | string | Display name/description (max 127 chars) |
| `state` | `"active"` or `"done"` | |
| `start_ts` | string (float) | Unix epoch |
| `end_ts` | string (float) or absent | Only when `state == "done"` |

**Start an activity**:
```
HSET kpidash:activity:{id} activity_id {id} host {host} name {name} state active start_ts {ts}
ZADD kpidash:activities {ts} {id}
ZREMRANGEBYRANK kpidash:activities 0 -21
```
`ZREMRANGEBYRANK` trims to the 20 most-recent entries (dashboard shows top 10;
extra 10 provide buffer for concurrent updates).

**End an activity**:
```
HSET kpidash:activity:{id} state done end_ts {ts}
```

**Dashboard reads**:
```
ZREVRANGE kpidash:activities 0 9       → [id1, id2, … id10]
HGETALL kpidash:activity:{id}          → per-activity fields
```

---

### Repo Status

| Key | Type | Written by | Read by | TTL |
|-----|------|-----------|---------|-----|
| `kpidash:repos:{hostname}` | HASH (field=path, value=JSON) | client (~30s) | dashboard | 30 s |

**Hash value format** (per repo path):
```json
{
  "name": "kpidash",
  "path": "/home/ken/src/kpidash",
  "branch": "001-mvp-dashboard",
  "is_dirty": true,
  "ts": 1743292801.0
}
```

Only repos where `branch != default_branch OR is_dirty == true` are written.
On cleanup (repo returns to clean state):
```
HDEL kpidash:repos:kubs0 /home/ken/src/kpidash
```

Dashboard reads:
```
HGETALL kpidash:repos:{hostname}
```
for each known hostname.

---

### Fortune

| Key | Type | Written by | Read by | TTL |
|-----|------|-----------|---------|-----|
| `kpidash:fortune:current` | STRING (JSON) | dashboard (rotation) | dashboard | none |
| `kpidash:fortune:pushed` | STRING (JSON) | any client | dashboard | configured (default 300 s) |

**Value format** (both keys):
```json
{
  "text": "Unix is user-friendly. It's just choosy about who its friends are.",
  "source": "pushed",
  "pushed_by": "kubs0"
}
```

`source` is `"local"` for `fortune` command output, `"pushed"` for client
pushes. `pushed_by` MAY be omitted when `source == "local"`.

**Dashboard display priority**: `fortune:pushed` (if key exists) takes
precedence over `fortune:current`. Dashboard checks `fortune:pushed` first on
each fortune refresh tick.

**Client push**:
```
SET kpidash:fortune:pushed '{"text":"...","source":"pushed","pushed_by":"kubs0"}' EX 300
```

---

### Status Messages

| Key | Type | Written by | Read by | TTL |
|-----|------|-----------|---------|-----|
| `kpidash:status:current` | STRING (JSON) | dashboard | clients, dashboard | none |
| `kpidash:status:ack:{message_id}` | STRING (`"1"`) | client CLI | dashboard | 60 s |

**`kpidash:status:current` value format**:
```json
{
  "message_id": "550e8400-e29b-41d4-a716-446655440000",
  "severity": "warning",
  "message": "Telemetry collection error on widget: disk[1]",
  "ts": 1743292802.0
}
```

`severity` values: `"warning"`, `"error"`.

**Acknowledge flow**:
1. Client reads `kpidash:status:current` to obtain `message_id`.
2. Client writes `SET kpidash:status:ack:{message_id} 1 EX 60`.
3. Dashboard polls for `EXISTS kpidash:status:ack:{message_id}` on each 1s
   tick. On match: DEL ack key, pop internal message queue, update
   `kpidash:status:current` (or DEL it if queue empty).

---

### System Info

| Key | Type | Written by | Read by | TTL |
|-----|------|-----------|---------|-----|
| `kpidash:system:logpath` | STRING | dashboard on start | clients | none (refreshed on restart) |
| `kpidash:system:version` | STRING | dashboard on start | clients | none |

**Values**: Plain strings (not JSON).

```
kpidash:system:logpath  →  "/var/log/kpidash/kpidash.log"
kpidash:system:version  →  "1.0.0"
```

---

## Access Patterns Summary

### Dashboard (1-second poll cycle)

```
SMEMBERS kpidash:clients                          → all hostnames
  for each hostname:
    GET  kpidash:client:{h}:health                → online/offline status
    GET  kpidash:client:{h}:telemetry             → metrics
    HGETALL kpidash:repos:{h}                     → repo status
EXISTS kpidash:status:ack:{current_message_id}    → check for ack
ZREVRANGE kpidash:activities 0 9                  → top-10 activity IDs
  for each id:
    HGETALL kpidash:activity:{id}                 → activity detail

FORTUNE TICK (every 300 s or configurable):
  EXISTS kpidash:fortune:pushed                   → pushed fortune present?
  if yes: GET kpidash:fortune:pushed
  else:   run `fortune`, SET kpidash:fortune:current
```

All reads use a short 1 ms non-blocking timeout on hiredis `redisCommand`. The
entire poll cycle is expected to complete in < 10 ms on localhost Redis.

### Client Daemon (continuous)

```
On startup:
  SADD kpidash:clients {hostname}
  SET  kpidash:system:logpath ...   [if MCP/debug agent]

Every 3 s:
  SET kpidash:client:{h}:health '{...}' EX 10

Every 5 s (configurable):
  SET kpidash:client:{h}:telemetry '{...}' EX 15

Every 30 s:
  For each repo in watched set (non-clean):
    HSET kpidash:repos:{h} {path} '{...}'
    EXPIRE kpidash:repos:{h} 30
  For each repo now clean:
    HDEL kpidash:repos:{h} {path}
```

### Client CLI

| Command | Redis operation |
|---------|----------------|
| `kpidash-client activity start "name"` | HSET + ZADD + ZREMRANGEBYRANK |
| `kpidash-client activity done` | HSET (state=done, end_ts) |
| `kpidash-client status ack` | GET current → SET ack:{id} EX 60 |
| `kpidash-client log-path` | GET kpidash:system:logpath → print |
| `kpidash-client fortune push "text"` | SET kpidash:fortune:pushed '...' EX N |

### MCP Server Tools

| Tool | Equivalent Redis operation |
|------|--------------------------|
| `start_activity(name, host)` | Same as CLI activity start |
| `end_activity(activity_id)` | Same as CLI activity done |

---

## Authentication

All components use the `REDISCLI_AUTH` environment variable as the Redis
password. No hard-coded passwords anywhere. Components MUST fail with a clear
error message if `REDISCLI_AUTH` is set but authentication fails.

Default Redis port: `6379`. Configurable in client config file only; not
runtime-switched without config change.
