# MCP Server Tools Contract: kpidash MVP

**Version**: 1.0 (MVP)
**Server name**: `kpidash`
**Transport**: stdio (default for local MCP servers)

This document defines the MCP tools exposed by `kpidash-mcp`. All tools
communicate with the Redis server on the Pi 5 using the schema defined in
`redis-schema.md`.

---

## Configuration

The MCP server reads Redis connection settings from environment variables:

| Variable | Required | Default | Notes |
|----------|----------|---------|-------|
| `KPIDASH_REDIS_HOST` | no | `localhost` | Redis server hostname or IP |
| `KPIDASH_REDIS_PORT` | no | `6379` | Redis server port |
| `REDISCLI_AUTH` | yes* | — | Redis password; if absent and Redis has no auth, omit |

---

## Tools

### `start_activity`

Start a named activity. Generates an `activity_id` and writes it to Redis.
Returns the activity ID so the caller can end it later.

**Input schema**:
```json
{
  "name": {
    "type": "string",
    "description": "Human-readable name or description of the activity",
    "maxLength": 127
  },
  "host": {
    "type": "string",
    "description": "Identifier for the agent or machine starting the activity. Defaults to the MCP server's hostname if omitted.",
    "maxLength": 63
  }
}
```

`host` is optional; defaults to the system hostname of the machine running the
MCP server.

**Returns**:
```json
{
  "activity_id": "550e8400-e29b-41d4-a716-446655440000",
  "host": "kubs0",
  "name": "Implementing kpidash MVP plan",
  "state": "active",
  "start_ts": 1743292800.0
}
```

**On error**: Returns an error result with a descriptive message (e.g., Redis
unreachable, name too long).

---

### `end_activity`

Mark an activity as done. Records the current wall-clock time as `end_ts`.

**Input schema**:
```json
{
  "activity_id": {
    "type": "string",
    "description": "The activity_id returned by start_activity"
  }
}
```

**Returns**:
```json
{
  "activity_id": "550e8400-e29b-41d4-a716-446655440000",
  "state": "done",
  "end_ts": 1743293400.0,
  "duration_seconds": 600.0
}
```

**On error**: Returns an error result if `activity_id` is not found in Redis
or Redis is unreachable.

---

## Error Handling

All tools return structured errors compatible with the MCP SDK error response
format. Errors do not raise exceptions visible to the caller — they return an
error result with `is_error: true` and a `content` message. The calling agent
(Copilot, Claude, etc.) receives the error message as a tool result.

Common error conditions:
- Redis connection failure (host/port unreachable)
- Redis authentication failure
- Activity ID not found
- Input validation failure (name too long, etc.)

---

## Notes

- The MCP server is stateless between tool calls; it opens a Redis connection
  per call (or maintains a short-lived pool — implementation detail).
- The `activity_id` must be tracked by the calling agent across `start` →
  `end` calls. It is the caller's responsibility to call `end_activity` when
  work concludes.
- For MVP, the MCP server exposes only these two tools. Additional tools
  (fortune push, status acknowledge, log-path retrieval) are candidates for
  future sprints.
