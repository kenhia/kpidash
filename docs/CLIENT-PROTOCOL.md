# KPI Dashboard — Client Protocol Specification

> **Version**: 2.0 (Sprint 002)
> **Transport**: Redis 7.x key/value operations
> **Auth**: `REDISCLI_AUTH` environment variable
> **Encoding**: JSON values in Redis strings/hashes

This document is the canonical reference for all Redis key names, value
formats, TTLs, and access patterns for the kpidash system. All components
(dashboard C binary, Python client daemons, MCP server) MUST conform to this
schema. See also `specs/001-mvp-dashboard/contracts/redis-schema.md` for the
authoritative source during development.

---

## Key Naming Conventions

- All keys are prefixed `kpidash:`.
- Separator: `:`.
- Hostnames are lowercase, matching the system `hostname` output.
- JSON field names use `snake_case`.

---

## 1. Client Registry

| Key | Type | Written by | Read by | TTL |
|-----|------|-----------|---------|-----|
| `kpidash:clients` | SET | client (startup) | dashboard | none |

```
SADD kpidash:clients {hostname}
```

Dashboard enumerates clients with `SMEMBERS kpidash:clients`.

---

## 2. Health

| Key | Type | Written by | Read by | TTL |
|-----|------|-----------|---------|-----|
| `kpidash:client:{hostname}:health` | STRING (JSON) | client (~3 s) | dashboard | **5 s** |

```json
{
  "hostname": "kubs0",
  "last_seen_ts": 1743292800.123,
  "uptime_seconds": 86400.5,
  "os_name": "Ubuntu 22.04.5 LTS"
}
```

`uptime_seconds` is optional. `os_name` is optional (from `/etc/os-release`
`PRETTY_NAME`, fallback to `platform.system() + " " + platform.release()`).
Key absence (TTL expired) = client offline.

```
SET kpidash:client:kubs0:health '{...}' EX 5
```

---

## 3. Telemetry

| Key | Type | Written by | Read by | TTL |
|-----|------|-----------|---------|-----|
| `kpidash:client:{hostname}:telemetry` | STRING (JSON) | client (~5 s) | dashboard | 15 s |

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

`gpu` may be `null` or omitted when no GPU present. `type` values: `"nvme"`,
`"ssd"`, `"hdd"`, `"other"`.

---

## 4. Activities

| Key | Type | Written by | Read by | TTL |
|-----|------|-----------|---------|-----|
| `kpidash:activities` | ZSET (score=start_ts) | any client | dashboard | none |
| `kpidash:activity:{uuid}` | HASH | any client | dashboard | none |

**HASH fields**: `activity_id`, `host`, `name`, `state` (`active`/`done`),
`start_ts` (string float), `end_ts` (string float, only when done).

**Start**:
```
HSET kpidash:activity:{id}  activity_id {id}  host {h}  name {n}  state active  start_ts {ts}
ZADD kpidash:activities {ts} {id}
ZREMRANGEBYRANK kpidash:activities 0 -21    # keep 20 most-recent
```

**End**:
```
HSET kpidash:activity:{id}  state done  end_ts {ts}
```

**Dashboard reads**:
```
ZREVRANGE kpidash:activities 0 9
HGETALL kpidash:activity:{id}
```

---

## 5. Repo Status

| Key | Type | Written by | Read by | TTL |
|-----|------|-----------|---------|-----|
| `kpidash:repos:{hostname}` | HASH (field=path, value=JSON) | client (~30 s) | dashboard | 30 s |

```json
{
  "name": "kpidash",
  "path": "/home/ken/src/kpidash",
  "branch": "002-exploration-sprint",
  "default_branch": "main",
  "is_dirty": true,
  "explicit": true,
  "detached_head": false,
  "ahead": 5,
  "behind": 0,
  "untracked_count": 2,
  "changed_count": 3,
  "deleted_count": 0,
  "renamed_count": 0,
  "last_commit_ts": 1743292600.0,
  "ts": 1743292801.0
}
```

| Field | Source | Notes |
|-------|--------|-------|
| `default_branch` | Client detection | For branch color logic |
| `detached_head` | `repo.head.is_detached` | Boolean |
| `ahead` / `behind` | `git rev-list --left-right --count HEAD...@{upstream}` | 0 if no upstream |
| `untracked_count` | `len(repo.untracked_files)` | |
| `changed_count` | Modified/added/typed changes in working tree | |
| `deleted_count` | Deleted files in working tree | |
| `renamed_count` | Renamed files in working tree | |
| `last_commit_ts` | `repo.head.commit.committed_date` (Unix epoch) | Used for "age" display |

`explicit: true` when the repo was listed under `[repos] explicit` in the
client config; `false` (or absent) for repos found via `scan_roots`.
The dashboard sorts explicit repos to the top of the Repo Status widget.

Only repos where `branch != default_branch OR is_dirty` are written. Clean
repos on their default branch are removed with `HDEL`.

---

## 6. Fortune

| Key | Type | Written by | Read by | TTL |
|-----|------|-----------|---------|-----|
| `kpidash:fortune:current` | STRING (JSON) | dashboard | dashboard | none |
| `kpidash:fortune:pushed` | STRING (JSON) | any client | dashboard | configured (default 300 s) |

```json
{ "text": "...", "source": "pushed", "pushed_by": "kubs0" }
```

`source`: `"local"` for `fortune` popen output, `"pushed"` for client pushes.
`pushed_by` may be omitted when `source == "local"`.

Pushed fortune overrides the rotation timer while the key exists.

**Client push**:
```
SET kpidash:fortune:pushed '{"text":"...","source":"pushed","pushed_by":"kubs0"}' EX 300
```

---

## 7. Status Messages

| Key | Type | Written by | Read by | TTL |
|-----|------|-----------|---------|-----|
| `kpidash:status:current` | STRING (JSON) | dashboard | clients | none |
| `kpidash:status:ack:{message_id}` | STRING (`"1"`) | client CLI | dashboard | 60 s |

```json
{
  "message_id": "550e8400-e29b-41d4-a716-446655440000",
  "severity": "warning",
  "message": "fortune binary not found — using canned messages",
  "ts": 1743292802.0
}
```

**Acknowledge flow**:
1. `GET kpidash:status:current` → extract `message_id`
2. `SET kpidash:status:ack:{message_id} 1 EX 60`
3. Dashboard detects ack on next 1 s poll → advances queue

---

## 8. Dev Telemetry (Sprint 002)

| Key | Type | Written by | Read by | TTL |
|-----|------|-----------|---------|-----|
| `kpidash:client:{hostname}:dev_telemetry` | STRING (JSON) | client (~1 s) | dashboard | 5 s |

```json
{
  "hostname": "kubs0",
  "ts": 1743292800.789,
  "cpu_pct": 12.3,
  "top_core_pct": 45.6,
  "ram_used_mb": 6144,
  "ram_total_mb": 16384,
  "gpu_compute_pct": 8.5,
  "gpu_vram_used_mb": 431,
  "gpu_vram_total_mb": 16376
}
```

High-frequency telemetry stream for the dev graph widget. Written at
`dev_interval_s` (default 1 s, configurable in `[client]` config section).
Contains only the 5 metrics used by the graph (GPU compute, CPU avg, CPU top,
VRAM, RAM). Dashboard reads this key only when the graph is enabled.

---

## 9. Dev Commands (Sprint 002)

Command keys enable/disable developer overlay widgets. All keys use JSON
values with a TTL of 300 s (auto-expire).

### 9.1 Grid Overlay

| Key | Type | Written by | Read by | TTL |
|-----|------|-----------|---------|-----|
| `kpidash:cmd:grid` | STRING (JSON) | manual (redis-cli) | dashboard | 300 s |

**Pixel-based mode** (original):
```json
{"enabled": true, "size": 50}
```

**Unit-based mode** (Sprint 003):
```json
{"enabled": true, "unit": true, "size": 1}
```

| Field | Type | Notes |
|-------|------|-------|
| `enabled` | bool | Show/hide grid overlay |
| `size` | int/float | Pixel spacing (pixel mode) or unit multiplier (unit mode) |
| `unit` | bool | Optional. When `true`, `size` is a unit multiplier (0.5, 1, 2) and grid lines are drawn at interior cell boundaries using `layout.h` constants. When absent or `false`, pixel-based mode (unchanged from Sprint 002). |

```bash
# Pixel grid (50px spacing)
redis-cli SET kpidash:cmd:grid '{"enabled":true,"size":50}' EX 300

# Unit grid (1-unit intervals — lines at cell boundaries)
redis-cli SET kpidash:cmd:grid '{"enabled":true,"unit":true,"size":1}' EX 300

# Half-unit intervals
redis-cli SET kpidash:cmd:grid '{"enabled":true,"unit":true,"size":0.5}' EX 300

# Double-unit intervals (2×1 widget boundaries)
redis-cli SET kpidash:cmd:grid '{"enabled":true,"unit":true,"size":2}' EX 300
```

### 9.2 Text Size Reference

| Key | Type | Written by | Read by | TTL |
|-----|------|-----------|---------|-----|
| `kpidash:cmd:textsize` | STRING (JSON) | manual (redis-cli) | dashboard | 300 s |

```json
{"enabled": true}
```

Shows sample text at various font sizes for visual calibration.

### 9.3 Dev Graph

| Key | Type | Written by | Read by | TTL |
|-----|------|-----------|---------|-----|
| `kpidash:cmd:graph` | STRING (JSON) | manual (redis-cli) | dashboard | 300 s |

```json
{"enabled": true, "client": "kubs0"}
```

| Field | Type | Notes |
|-------|------|-------|
| `enabled` | bool | Show/hide graph widget |
| `client` | string | Hostname whose dev_telemetry to graph |

Enables a 5-series time-series chart (GPU compute, CPU avg, CPU top, VRAM,
RAM) displayed in Row 1 columns 0–1 (absolute positioned, 2×1 units). Uses
300 data points at 1s intervals (5 min history). Reads from
`kpidash:client:{client}:dev_telemetry`.

```bash
redis-cli SET kpidash:cmd:graph '{"enabled":true,"client":"kubs0"}' EX 300
```

---

## 10. System Info

| Key | Type | Written by | Read by | TTL |
|-----|------|-----------|---------|-----|
| `kpidash:system:logpath` | STRING | dashboard (start) | clients | none |
| `kpidash:system:version` | STRING | dashboard (start) | clients | none |

Plain strings (not JSON). Written once on dashboard startup.

---

## Python Client Quick Reference

```python
# Minimal health ping loop
import redis, json, socket, time

r = redis.Redis(host="192.168.1.50", password="secret", decode_responses=True)
hostname = socket.gethostname().lower()
r.sadd("kpidash:clients", hostname)

while True:
    r.set(f"kpidash:client:{hostname}:health",
          json.dumps({"hostname": hostname, "last_seen_ts": time.time()}),
          ex=5)
    time.sleep(3)
```

Use `kpidash-client` for production use — it handles all keys, reconnection,
and cross-platform telemetry collection.
