# Redis Contract — Dev Telemetry, `host` Field Extension

**Sprint**: 006 (extends sprint 002's dev-telemetry key)
**Key family**: `kpidash:client:<hostname>:dev_telemetry`
**Type**: STRING (JSON value)
**Written by**: `kpidash-client` daemon (per existing dev-telemetry interval, typically ~1 s)
**Read by**: kpidash dashboard
**TTL**: 5 s (unchanged from sprint 002)

---

## What changes in sprint 006

1. Each sample's JSON payload gains a required `host` field whose value is the reporting host's name (equal to `<hostname>` in the key).
2. The dashboard no longer requires `kpidash:cmd:graph` to be set in order to read this key; the Graph widget is always-on (FR-010), and the dashboard discovers reporting hosts by `SCAN MATCH kpidash:client:*:dev_telemetry`.
3. The dashboard maintains one Graph widget per distinct `host` value observed in a payload (FR-012).

The sprint-002 fields (`cpu_pct`, `top_core_pct`, `ram_used_mb`, `ram_total_mb`, `gpu.*`) are unchanged.

## Value (JSON)

```json
{
  "host": "kai",
  "ts": 1748000000.123,
  "cpu_pct": 14.2,
  "top_core_pct": 88.5,
  "ram_used_mb": 9216,
  "ram_total_mb": 32768,
  "gpu": {
    "compute_pct": 42.0,
    "vram_used_mb": 7340,
    "vram_total_mb": 24576
  }
}
```

| Field        | Type    | Required | Notes |
|--------------|---------|----------|-------|
| `host`       | string  | yes (new) | Reporting host name. Must equal `<hostname>` in the key. |
| `ts`         | number  | yes      | Publisher wall-clock. Dashboard uses this for stale detection (30 s threshold, FR-014). |
| `cpu_pct`, `top_core_pct`, `ram_used_mb`, `ram_total_mb`, `gpu.*` | — | per sprint 002 | Unchanged. |

## Writer rules

- The Python client (`clients/kpidash-client`) MUST include `host` in every dev-telemetry payload starting sprint 006.
- TTL remains 5 s: `SET kpidash:client:<hostname>:dev_telemetry <json> EX 5`.

## Reader rules (dashboard)

- On each poll cycle, `SCAN MATCH kpidash:client:*:dev_telemetry`; `GET` each value; parse JSON.
- If `host` is present, use it for routing to the per-host Graph widget. If `host` is missing (legacy publisher not yet upgraded), route the sample under a synthetic host label `"(legacy)"` so the data remains visible until the publisher upgrades.
- Update `last_sample_ts` of the host series to the dashboard's wall-clock at apply time (NOT to payload `ts`; this isolates stale detection from publisher clock skew). The stale-overlay rule of FR-014 fires when `now - last_sample_ts >= 30.0`.

## Backwards compatibility

- Publishers from sprint 002 / 003 / 004 / 005 omit `host`. Their samples are accepted and routed under `"(legacy)"`. Operator action: upgrade `kpidash-client` on each reporting host.
- The retired key `kpidash:cmd:graph` (sprint 002 §9.3) is no longer read; it MAY be deleted at any time. Documentation in `docs/CLIENT-PROTOCOL.md` will be updated during implementation to mark §9.3 as removed.
