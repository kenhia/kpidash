# KPI Dashboard — Client Protocol Specification

> **Version**: 0.1 (POC)
> **Transport**: UDP
> **Port**: 5555
> **Encoding**: UTF-8 JSON

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
