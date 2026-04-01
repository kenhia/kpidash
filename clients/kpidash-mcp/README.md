# kpidash-mcp

An MCP (Model Context Protocol) server that lets AI agents (GitHub Copilot,
Claude, etc.) report their activity to the
[kpidash](https://github.com/your-org/kpidash) dashboard display.

---

## Requirements

- Python 3.11+
- `mcp>=1` (installed automatically)
- Access to the Redis server on the Pi 5

---

## Installation

```bash
# from source (uv recommended — ensures Python 3.13):
cd /path/to/kpidash/clients/kpidash-mcp
uv sync                  # creates .venv with Python 3.13, installs deps
uv run kpidash-mcp       # verify (exits immediately — expects MCP transport)
```

---

## Environment variables

| Variable | Required | Default | Description |
|----------|----------|---------|-------------|
| `KPIDASH_REDIS_HOST` | no | `localhost` | Redis host |
| `KPIDASH_REDIS_PORT` | no | `6379` | Redis port |
| `REDISCLI_AUTH` | yes* | — | Redis password (omit if no auth) |

---

## MCP tools

### `start_activity`

Start a named activity. Returns the `activity_id` needed to end it later.

**Inputs:**

| Field  | Type   | Required | Description |
|--------|--------|----------|-------------|
| `name` | string | yes      | Human-readable description (max 127 chars) |
| `host` | string | no       | Agent/machine identifier (max 63 chars; defaults to server hostname) |

**Returns:**
```json
{
  "activity_id": "550e8400-e29b-41d4-a716-446655440000",
  "host": "kubs0",
  "name": "Implementing kpidash MVP plan",
  "state": "active",
  "start_ts": 1743292800.0
}
```

---

### `end_activity`

Mark an activity as done.

**Inputs:**

| Field         | Type   | Required | Description |
|---------------|--------|----------|-------------|
| `activity_id` | string | yes      | UUID returned by `start_activity` |

**Returns:**
```json
{
  "activity_id": "550e8400-e29b-41d4-a716-446655440000",
  "state": "done",
  "end_ts": 1743293400.0,
  "duration_seconds": 600.0
}
```

---

## Configuration examples

### Claude Desktop (`claude_desktop_config.json`)

```json
{
  "mcpServers": {
    "kpidash": {
      "command": "/path/to/kpidash/clients/kpidash-mcp/.venv/bin/kpidash-mcp",
      "env": {
        "KPIDASH_REDIS_HOST": "192.168.1.50",
        "REDISCLI_AUTH": "yourpassword"
      }
    }
  }
}
```

### GitHub Copilot (`.vscode/settings.json`)

```json
{
  "github.copilot.chat.mcp.servers": {
    "kpidash": {
      "command": "/path/to/kpidash/clients/kpidash-mcp/.venv/bin/kpidash-mcp",
      "env": {
        "KPIDASH_REDIS_HOST": "192.168.1.50",
        "REDISCLI_AUTH": "yourpassword"
      }
    }
  }
}
```

> **uv note**: MCP hosts launch the server process directly, so specify the
> full path to the binary inside the `.venv` rather than `uv run kpidash-mcp`.

### Manual test from shell

```bash
export KPIDASH_REDIS_HOST=192.168.1.50
export REDISCLI_AUTH=yourpassword
kpidash-mcp
# Server listens on stdin/stdout; send JSON-RPC messages to test
```

---

## Notes

- The MCP server is **stateless** between tool calls; it opens a fresh Redis
  connection for each tool invocation.
- The AI agent is responsible for tracking the `activity_id` across
  `start_activity` → `end_activity` calls within a session.
- Errors (Redis unreachable, activity ID not found, name too long) are returned
  as `isError: true` content — the agent sees a descriptive error message.

---

## Development

```bash
cd /path/to/kpidash/clients/kpidash-mcp
uv sync
uv run ruff format --check . && uv run ruff check . && uv run pytest -q
```
