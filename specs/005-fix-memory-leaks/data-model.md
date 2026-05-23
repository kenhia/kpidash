# Phase 1 Data Model: Memory Leak Investigation

**Feature**: 005-fix-memory-leaks  
**Date**: 2026-05-16

The work is diagnostic + corrective; there is no user-facing data schema. The entities below are in-process structures and a documentation format for tracking the investigation.

## Entity: `mem_sample_t`

The single point-in-time observation emitted by the periodic sampler. Lives in `src/memstat.h`.

| Field | C type | Source | Units | Notes |
|---|---|---|---|---|
| `t` | `time_t` | `time(NULL)` | seconds since epoch | Sample timestamp. |
| `uptime_s` | `uint64_t` | monotonic clock delta from process start | seconds | Used to align samples within a soak. |
| `rss_bytes` | `uint64_t` | `/proc/self/statm` field 2 × `sysconf(_SC_PAGESIZE)` | bytes | Process resident set size. |
| `vsize_bytes` | `uint64_t` | `/proc/self/statm` field 1 × page size | bytes | Optional secondary signal. |
| `lvgl_total` | `uint32_t` | `lv_mem_monitor_t.total_size` | bytes | LVGL heap capacity. |
| `lvgl_free` | `uint32_t` | `lv_mem_monitor_t.free_size` | bytes | Currently free. |
| `lvgl_used` | `uint32_t` | derived `total − free` | bytes | Currently used. |
| `lvgl_max_used` | `uint32_t` | `lv_mem_monitor_t.max_used` | bytes | Watermark — primary leak signal. |
| `lvgl_frag_pct` | `uint8_t` | `lv_mem_monitor_t.frag_pct` | percent (0–100) | Fragmentation. |
| `lvgl_free_biggest` | `uint32_t` | `lv_mem_monitor_t.free_biggest_size` | bytes | Largest free block. |

**Invariants**:
- `lvgl_free + lvgl_used == lvgl_total` (sanity check after each sample).
- `lvgl_max_used ≥ lvgl_used` (monotonically non-decreasing over the process lifetime).
- `uptime_s` strictly increases between successive samples.

**Lifecycle**: Stack-allocated in the timer callback, serialized to one log line and to JSON for Redis, then discarded. No in-process retention beyond the current sample (the Redis ring is the time-series store).

## Entity: `leak_source` (documentation record)

Not a C struct — a markdown record kept alongside this spec to satisfy FR-011. Each identified leak gets one entry in `specs/005-fix-memory-leaks/findings.md` (created during Phase 2 / implementation, not now). Schema:

```markdown
### L00N — <short title>

- **Location**: <file>:<line(s)>, function <name>
- **Nature**: object | style | chart series | image/canvas buffer | font | other
- **Evidence**: which `mem_sample_t` series demonstrated the leak (e.g., "lvgl_max_used rose monotonically by ~4 KB per `activities_widget_update` call under registry churn"); reference the regression test name that reproduces it.
- **Root cause**: one sentence.
- **Fix**: one sentence + commit / PR reference.
- **Regression guard**: name of the assertion in `tests/test_widget_leak.c` that now covers this path.
```

This record exists so the "what did we change and why" is auditable without re-reading the diff.

## Telemetry log line format

Single fixed-shape line emitted at INFO level, prefixed `memstat:` for easy `grep`:

```text
memstat: t=<unix> up=<seconds> rss=<bytes> vsize=<bytes> lvgl_used=<bytes> lvgl_max=<bytes> lvgl_total=<bytes> lvgl_frag=<pct> lvgl_free_biggest=<bytes>
```

Stable, greppable, awk-friendly. The quickstart's post-processing one-liners rely on this exact prefix and key names.
