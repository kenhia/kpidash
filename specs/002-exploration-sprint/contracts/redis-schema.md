# Redis Schema Changes: Exploration Sprint

**Date**: 2026-04-03
**Baseline**: [specs/001-mvp-dashboard/contracts/redis-schema.md](../../001-mvp-dashboard/contracts/redis-schema.md)

This document describes **additions** to the existing Redis schema. All existing keys are unchanged.

## Modified Keys

### `kpidash:client:{hostname}:health` (STRING, TTL 5s)

**Change**: Add `os_name` field to JSON payload.

**Before** (001-mvp):
```json
{
  "hostname": "kubs0",
  "last_seen_ts": 1743292800.123,
  "uptime_seconds": 86400.5
}
```

**After** (002-exploration):
```json
{
  "hostname": "kubs0",
  "last_seen_ts": 1743292800.123,
  "uptime_seconds": 86400.5,
  "os_name": "Linux 5.15.0-173-generic"
}
```

| Field | Type | Required | Notes |
|-------|------|----------|-------|
| `os_name` | string | No | Client generates via `platform.system() + " " + platform.release()`. Dashboard displays empty string if absent (backward compatible). |

**Backward compatibility**: The C parser treats `os_name` as optional. Clients running the old
daemon (without `os_name`) will not break the dashboard — the field defaults to empty string.

## New Keys

### `kpidash:cmd:grid` (STRING, TTL 300s)

**Purpose**: Development command to toggle grid overlay on the dashboard.

**Writer**: Any Redis client (typically via `redis-cli` or a dev script).
**Reader**: Dashboard (`redis_poll()` cycle).

```json
{"enabled": true, "size": 50}
```

| Field | Type | Required | Notes |
|-------|------|----------|-------|
| `enabled` | boolean | Yes | `true` = show grid, `false` = hide grid |
| `size` | integer | Yes (when enabled) | Grid spacing in pixels. Must be > 0. |

**Publish example**:
```bash
redis-cli SET kpidash:cmd:grid '{"enabled":true,"size":50}' EX 300
redis-cli SET kpidash:cmd:grid '{"enabled":false}' EX 10
```

**TTL behavior**: Key auto-expires after 300s. Dashboard removes overlay when key absent or expired.

### `kpidash:cmd:textsize` (STRING, TTL 300s)

**Purpose**: Development command to toggle text size reference widget.

**Writer**: Any Redis client.
**Reader**: Dashboard (`redis_poll()` cycle).

```json
{"enabled": true}
```

| Field | Type | Required | Notes |
|-------|------|----------|-------|
| `enabled` | boolean | Yes | `true` = show widget, `false` = hide widget |

**Publish example**:
```bash
redis-cli SET kpidash:cmd:textsize '{"enabled":true}' EX 300
redis-cli DEL kpidash:cmd:textsize
```

## Key Summary (002 additions)

| Key | Type | TTL | Writer | Reader |
|-----|------|-----|--------|--------|
| `kpidash:cmd:grid` | STRING (JSON) | 300s | developer | dashboard |
| `kpidash:cmd:textsize` | STRING (JSON) | 300s | developer | dashboard |

## Protocol Constants (protocol.h additions)

```c
#define KPIDASH_KEY_CMD_GRID     "kpidash:cmd:grid"
#define KPIDASH_KEY_CMD_TEXTSIZE "kpidash:cmd:textsize"
#define CMD_TTL_S                300
```
