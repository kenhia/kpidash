# KPI Dashboard — Client Protocol Specification

> **Version**: 1.0 (MVP)
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
  "uptime_seconds": 86400.5
}
```

`uptime_seconds` is optional. Key absence (TTL expired) = client offline.

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
  "branch": "001-mvp-dashboard",
  "is_dirty": true,
  "ts": 1743292801.0
}
```

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

## 8. System Info

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


This document specifies the protocol that client applications use to
communicate with the KPI Dashboard running on the Raspberry Pi. It is intended
as a hand-off document for an agent or developer building a client on any
platform.

---

## 1. Transport

- **Protocol**: UDP (connectionless, fire-and-forget).
- **Dashboard listens on**: `0.0.0.0:5555` (configurable via `KPIDASH_PORT`
  environment variable, default `5555`).
- **Packet size**: each message must fit in a single UDP datagram. Maximum
  payload is **1024 bytes** (well under the typical MTU).
- **No acknowledgment**: the dashboard does not reply. Clients should send
  messages periodically; the dashboard tolerates missed packets.

## 2. Message Format

Every message is a single JSON object with a required `type` field.

```json
{
    "type": "<message_type>",
    "host": "<client_hostname>",
    ...type-specific fields...
}
```

### Common Fields

| Field | Type | Required | Description |
|-------|------|----------|-------------|
| `type` | string | yes | Message type: `"health"` or `"task"` |
| `host` | string | yes | Client hostname (max 63 chars). Used as the unique client identifier. |

### 2.1 Health Ping

Sent periodically (recommended: every 2–5 seconds) to indicate the client is
alive.

```json
{
    "type": "health",
    "host": "kubs0",
    "uptime": 123456.78
}
```

| Field | Type | Required | Description |
|-------|------|----------|-------------|
| `uptime` | number | no | Client system uptime in seconds (floating-point). Informational. |

**Dashboard behavior**:
- Records `last_health` timestamp for the client.
- If no health ping is received within `HEALTH_TIMEOUT` seconds (default: 10),
  the client's LED turns red.
- If the `host` value is new, a new client widget is created automatically.

### 2.2 Task Update

Sent when the user sets or changes their current task.

```json
{
    "type": "task",
    "host": "kubs0",
    "task": "kris development",
    "started": 1711650000
}
```

| Field | Type | Required | Description |
|-------|------|----------|-------------|
| `task` | string | yes | Human-readable task description (max 127 chars). |
| `started` | integer | yes | Unix epoch timestamp (seconds) when the task began. |

To **clear** a task, send:

```json
{
    "type": "task",
    "host": "kubs0",
    "task": "",
    "started": 0
}
```

**Dashboard behavior**:
- Updates the task label and elapsed-time display for the client.
- Sending a new task replaces the previous one (elapsed time resets).
- Elapsed time is computed as `now - started` and displayed as `HH:MM:SS`.
- A cleared task displays `(no task)` with `--:--:--` for elapsed.

### 2.3 Task Done

Sent when the user completes their current task.

```json
{
    "type": "task_done",
    "host": "kubs0"
}
```

No additional fields are required.

**Dashboard behavior**:
- Archives the current task and clears it.
- Displays a checkmark with the completed task name for ~60 seconds, then
  reverts to `(no task)`.
- If there is no active task, the message is a no-op.

## 3. Client Implementation Guide

### 3.1 Minimal Client (Python example)

```python
import socket
import json
import time

DASH_HOST = "192.168.1.100"  # Pi's IP address
DASH_PORT = 5555

sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)

def send_health(hostname: str, uptime: float):
    msg = json.dumps({"type": "health", "host": hostname, "uptime": uptime})
    sock.sendto(msg.encode(), (DASH_HOST, DASH_PORT))

def send_task(hostname: str, task: str, started: int):
    msg = json.dumps({
        "type": "task", "host": hostname,
        "task": task, "started": started
    })
    sock.sendto(msg.encode(), (DASH_HOST, DASH_PORT))

# Example: send a health ping every 3 seconds
import platform
hostname = platform.node()
while True:
    send_health(hostname, time.monotonic())
    time.sleep(3)
```

### 3.2 Minimal Client (curl / netcat for testing)

**bash/zsh:**
```bash
# Health ping
echo '{"type":"health","host":"testbox","uptime":1234}' | nc -u -w0 PI_IP 5555

# Task update
echo '{"type":"task","host":"testbox","task":"debugging","started":'"$(date +%s)"'}' | nc -u -w0 PI_IP 5555
```

**fish:**
```fish
echo '{"type":"health","host":"testbox","uptime":1234}' | nc -u -w0 PI_IP 5555
echo '{"type":"task","host":"testbox","task":"debugging","started":'(date +%s)'}' | nc -u -w0 PI_IP 5555
```

### 3.3 Recommended Client Architecture

For a production-quality client daemon:

```
┌──────────────────────────────────┐
│         Client Daemon            │
│                                  │
│  ┌────────────────────────────┐  │
│  │ Health Ping Thread         │  │
│  │ - Send health every N sec  │  │
│  │ - Include system uptime    │  │
│  └────────────────────────────┘  │
│                                  │
│  ┌────────────────────────────┐  │
│  │ Task Manager               │  │
│  │ - CLI or API to set task   │  │
│  │ - Sends task update on     │  │
│  │   change                   │  │
│  └────────────────────────────┘  │
│                                  │
│  ┌────────────────────────────┐  │
│  │ Configuration              │  │
│  │ - Dashboard IP / port      │  │
│  │ - Ping interval            │  │
│  │ - Hostname override        │  │
│  └────────────────────────────┘  │
└──────────────────────────────────┘
```

**Suggested CLI interface** (for the agent building the client):

```
kpidash-client health              # send one health ping
kpidash-client task "kris dev"     # set current task
kpidash-client task --clear        # clear current task
kpidash-client daemon              # run as background daemon (health pings + listen for task changes)
```

### 3.4 Configuration

Clients should support configuration via environment variables or a config
file:

| Variable | Default | Description |
|----------|---------|-------------|
| `KPIDASH_HOST` | `255.255.255.255` | Dashboard IP (broadcast if unknown) |
| `KPIDASH_PORT` | `5555` | Dashboard UDP port |
| `KPIDASH_HOSTNAME` | system hostname | Override reported hostname |
| `KPIDASH_HEALTH_INTERVAL` | `3` | Seconds between health pings |

## 4. Protocol Evolution Notes

This POC protocol is intentionally minimal. Planned future extensions:

- **Version field**: add `"v": 1` to messages for backward compatibility.
- **Message types**: `register`, `deregister`, `metric` (CPU, memory, etc.).
- **Acknowledgment**: optional TCP or request-reply for critical messages.
- **Authentication**: HMAC signature field for client identity verification.
- **Encryption**: DTLS wrapper for encrypted transport.

## 5. Error Handling

- **Malformed JSON**: the dashboard silently drops the packet and logs a
  warning to stderr.
- **Unknown message type**: silently dropped.
- **Oversized hostname/task**: truncated to fit the internal buffer (63 / 127
  chars respectively).
- **Unknown fields**: ignored (forward-compatible).
