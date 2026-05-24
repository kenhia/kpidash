# Phase 1 Data Model — Sprint 006

Entities introduced or modified by this sprint. All C structs live in `src/registry.h` unless noted otherwise.

---

## 1. Service Status Payload (wire format)

The JSON value stored at Redis key `kpidash:services:<name>`. Written by publishers (e.g. `kpidash-client service-status ...`); read by the dashboard.

| Field | Type | Required | Notes |
|-------|------|----------|-------|
| `ts` | number (Unix epoch seconds, float OK) | yes | Source timestamp; used by dashboard for the 60 s freshness check. |
| `state` | string enum: `ok` \| `unhealthy` \| `maintenance` \| `down` | yes | Drives card border colour (with freshness). Missing or unrecognised value → payload ignored (FR-022a). |
| `text` | string | yes | Short status text rendered on the card. |
| `host` | string | no | Optional host annotation. Free-form (e.g. `kai`, `kdash`); no host registry. |
| `icon` | integer | no | Index into the dashboard's icon registry (see §3). Unknown index → render no icon (FR-026). |

**Validation rules**:
- `ts`, `state`, `text` are required for a payload to be considered valid.
- A payload missing `state` OR whose `state` is not in the documented enum MUST be ignored (FR-022a). The previous valid state continues to drive the card; that previous valid state ages out of the 60 s freshness window normally. The dashboard MAY log the malformed payload but MUST NOT surface it visually.
- A future `ts` is accepted as-is (clock skew handling is out of scope).

**Example**:

```json
{
  "ts": 1748000000.123,
  "state": "unhealthy",
  "text": "queue depth 12k",
  "host": "kai",
  "icon": 7
}
```

---

## 2. Service Card State (in-memory)

C struct held per known service in the registry. Driven by the most recent valid payload plus dashboard-side wall-clock.

```c
typedef enum {
    SERVICE_STATE_UNKNOWN = 0,   /* no valid payload ever seen */
    SERVICE_STATE_OK,
    SERVICE_STATE_UNHEALTHY,
    SERVICE_STATE_MAINTENANCE,
    SERVICE_STATE_DOWN,
} service_state_t;

typedef struct {
    char name[64];           /* from key suffix */
    char host[64];           /* optional, "" if absent */
    char text[128];          /* last valid status text */
    int  icon_index;         /* -1 if absent or unknown */
    double last_payload_ts;  /* payload's own ts */
    service_state_t last_valid_state;
    /* LVGL handles (non-test builds only) */
    lv_obj_t *container;
    lv_obj_t *border;
    lv_obj_t *icon_label;
    lv_obj_t *name_label;
    lv_obj_t *text_label;
} service_entry_t;
```

### State machine (per service, evaluated each UI tick)

Inputs: `last_valid_state`, `last_payload_ts`, current wall-clock `now`.

Define `fresh := (now - last_payload_ts) < 60.0`.

| `last_valid_state` | `fresh` | Border colour |
|--------------------|---------|---------------|
| `OK` | yes | **Green** |
| `UNHEALTHY` | yes | **Yellow** |
| `MAINTENANCE` | yes | **Blue** |
| `DOWN` | (ignored) | **Gray** (no freshness applied — FR-019) |
| `OK` / `UNHEALTHY` / `MAINTENANCE` | no | **Red** |
| `UNKNOWN` | — | card not rendered (no entry exists yet) |

**Notes**:
- Per FR-019, `DOWN` is sticky-gray as long as the most recent valid payload says `down`, regardless of freshness.
- Per FR-022a, malformed payloads do NOT update `last_valid_state` or `last_payload_ts`; the entry keeps aging out as if the malformed payload never arrived.
- A service card is created the first time a valid payload appears for that key, and is not removed within a session (mirrors FR-015a's "no eviction" philosophy for graph hosts).

---

## 3. Icon Registry Entry

Owned by the dashboard. Static, compiled in. Defined in `src/icons.{c,h}`.

```c
typedef struct {
    int         index;        /* stable integer used in service payloads */
    uint32_t    codepoint;    /* nerd-font codepoint, e.g. 0xF300 */
    const char *label;        /* short human label for debugging / mockup catalogue */
} icon_entry_t;

extern const icon_entry_t ICON_REGISTRY[];
extern const size_t       ICON_REGISTRY_COUNT;
```

**Lookup contract**:
- `icons_lookup(int index) -> const char *utf8_glyph_or_NULL`
- Returns NULL when `index < 0` or `index` does not appear in the registry → caller renders no icon (FR-026).
- Successful lookup returns a static UTF-8 string (3- or 4-byte sequence for the glyph) suitable for direct use as an `lv_label` text.

**Indexing rule**: integer indices are assigned in declaration order in `ICON_REGISTRY[]` and are append-only — never renumber. Adding a new glyph appends a new entry with the next free index.

---

## 4. Graph Sample (wire format extension)

Existing key: `kpidash:client:<hostname>:dev_telemetry` (sprint 002).

**Change**: each sample's JSON value MUST include a `host` field equal to `<hostname>` from the key. Existing fields (`cpu_pct`, `top_core_pct`, `ram_used_mb`, `ram_total_mb`, `gpu.*`) are unchanged.

| Field | Type | Required | Notes |
|-------|------|----------|-------|
| `host` | string | yes (new) | Identifies the reporting host. If missing on a legacy publisher, the dashboard MUST treat the sample as if `host = "(legacy)"` so it remains visible (edge case in spec). |
| _(all sprint-002 fields)_ | — | unchanged | See `docs/CLIENT-PROTOCOL.md` §8. |

---

## 5. Graph Host Series (in-memory)

C struct held per discovered host that is currently or has ever published graph samples in this session.

```c
typedef struct {
    char     host[64];           /* "kai", "kdash", or "(legacy)" */
    double   last_sample_ts;     /* wall-clock when last sample was applied */
    /* Chart state owned by widgets/dev_graph.c */
    lv_obj_t *widget;            /* container (chart + overlay label) */
    bool     stale;              /* shadow of overlay visibility, for tests */
} graph_host_series_t;
```

**Discovery**: hosts are discovered by `SCAN MATCH kpidash:client:*:dev_telemetry` plus reading the `host` field from each payload (key-name and payload-`host` must agree; payload value wins for routing).

**Eviction (FR-015a)**: never within a session. A host series, once created, persists with its widget showing "NO NEW DATA" until the dashboard process restarts and the host is not re-observed during startup.

**Stale detection**: a host series is stale when `now - last_sample_ts >= 30.0`. The widget's overlay is shown iff stale.

**Placement**: when the rows-2-3 layout is composed, host series are emitted in ascending alphabetical order of `host` (FR-013); each consumes one Graph cell in the rows-2-3 priority pool.

---

## 6. Layout Pool (rows 2-3)

Pure-logic struct, no LVGL dependency — testable.

```c
typedef enum {
    WIDGET_GRAPH = 0,        /* highest priority; one per graph_host_series */
    WIDGET_ACTIVITIES,
    WIDGET_REPO_STATUS,
    WIDGET_FORTUNE,
    /* future widget kinds appended here, with explicit priority chosen at add-time */
} widget_kind_t;

typedef struct {
    widget_kind_t kind;
    uint8_t       cells;      /* size in grid cells; Fortune = 2, others = TBD per existing layout */
    const void   *payload;    /* opaque pointer used by the renderer (e.g., graph_host_series_t*) */
} widget_request_t;
```

**Placement rule (FR-003 / FR-004)**:

1. Sort requests by `kind` ascending (the enum order IS the priority order).
2. Greedily place from a 12-cell capacity counter; if a request's `cells` would exceed remaining capacity, **drop it** and continue evaluating lower-priority requests (the spec asks for "drop the lowest-priority widgets that do not fit and place the remaining widgets without resizing" — greedy with drop-and-continue satisfies this and handles the case where a smaller lower-priority widget can still fit after a larger one was dropped).
3. Multi-instance kinds (currently only `WIDGET_GRAPH`) are expanded into one request each, ordered alphabetically by host before the global sort (FR-013); the global sort is stable so the alphabetical order is preserved among the Graph entries.

**Capacity**: fixed at 12 cells (6 columns × 2 rows). Geometry constants stay in `src/layout.h` and are unchanged.

---

## Cross-references

| Entity | Drives | Spec FR |
|--------|--------|---------|
| Service Status Payload | Service Card State | FR-021, FR-022, FR-022a |
| Service Card State | Card border colour | FR-019, FR-020 |
| Icon Registry Entry | Card icon rendering | FR-023, FR-024, FR-026 |
| Graph Sample (with `host`) | Graph Host Series | FR-011, FR-012 |
| Graph Host Series | Per-host widget + stale overlay | FR-013, FR-014, FR-015, FR-015a |
| Layout Pool | Rows-2-3 placement | FR-002, FR-003, FR-004, FR-007 |
