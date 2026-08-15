# Phase 1 Contract: Redis Telemetry Keys

**Feature**: 005-fix-memory-leaks  
**Date**: 2026-05-16  
**Extends**: [docs/CLIENT-PROTOCOL.md](../../../docs/CLIENT-PROTOCOL.md) — the `kpidash:system:*` namespace

Two new keys are added to the existing `kpidash:system:*` namespace. They are written by the kpidash daemon itself (not by clients) and are intended for diagnostic consumption (dashboards, soak post-processing, ad-hoc inspection).

## Key: `kpidash:system:mem:current`

- **Type**: string (JSON)
- **TTL**: none (overwritten on every sample)
- **Writer**: kpidash, every 60 s
- **Reader**: humans (`redis-cli GET`), dashboards, post-processing scripts

**Payload schema** (fields match `mem_sample_t` in [data-model.md](../data-model.md)):

```json
{
  "t": 1747353600,
  "uptime_s": 86400,
  "rss_bytes": 70123520,
  "vsize_bytes": 142606336,
  "lvgl_used": 1048576,
  "lvgl_max_used": 1310720,
  "lvgl_total": 4194304,
  "lvgl_frag_pct": 7,
  "lvgl_free_biggest": 3014656
}
```

**Guarantees**:
- Written within 60 s of kpidash startup (per SC-004) and refreshed every 60 s thereafter.
- If the Redis connection is down, the sample is logged but the key write is skipped silently — when the connection returns, the next sample overwrites the key normally.
- The JSON object MAY gain new fields in future minor revisions; consumers MUST ignore unknown fields.

## Key: `kpidash:system:mem:ring`

- **Type**: list
- **Bound**: capped at 1500 entries (~25 h at 60 s cadence) via `LTRIM 0 1499` after each `LPUSH`
- **Writer**: kpidash, every 60 s — `LPUSH` then `LTRIM`
- **Reader**: post-processing scripts that want recent history without parsing the log

Each list entry is the same JSON payload as `kpidash:system:mem:current`. Index 0 is the most recent.

**Guarantees**:
- The ring never exceeds the cap — operators can `MONITOR` Redis without worrying about runaway memory in Redis itself.
- The ring is best-effort; the kpidash log is the source of truth for soak verdicts. If the ring is lost (Redis restart, manual `DEL`), the soak can still be verified from the log alone.

## Non-changes

- No existing key is renamed, retyped, or repurposed.
- No new keyspace prefix is introduced.
- The `kpidash:system:logpath` and `kpidash:system:version` keys (written by `redis_write_system_info()`) are untouched.
- No new pub/sub channels.

## Backwards compatibility

Clients of the existing client protocol (`kpidash-client`, `kpidash-mcp`) are not affected — they never read `kpidash:system:mem:*`. The documentation in `docs/CLIENT-PROTOCOL.md` will be updated during the polish phase to mention these diagnostic keys in the "kpidash-written keys" section.
