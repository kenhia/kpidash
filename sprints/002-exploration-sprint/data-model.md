# Data Model: Exploration Sprint

**Date**: 2026-04-03
**Plan**: [plan.md](plan.md) | **Spec**: [spec.md](spec.md)

## Entity Changes

### Modified: `client_info_t` (src/registry.h)

Added field for OS name display.

| Field | Type | Source | Notes |
|-------|------|--------|-------|
| `os_name` | `char[128]` | Health JSON `os_name` | New. `platform.system() + " " + platform.release()` from client |

All other existing fields unchanged. Full struct for reference:

```c
typedef struct {
    char         hostname[HOSTNAME_LEN];      // existing
    bool         online;                      // existing
    double       last_seen_ts;                // existing
    float        uptime_seconds;              // existing
    char         os_name[128];                // NEW — from health JSON
    float        cpu_pct;                     // existing
    float        top_core_pct;               // existing
    uint32_t     ram_used_mb;                // existing
    uint32_t     ram_total_mb;               // existing
    gpu_info_t   gpu;                        // existing
    disk_entry_t disks[MAX_DISKS];           // existing
    int          disk_count;                 // existing
    double       telemetry_ts;               // existing
    lv_obj_t    *container;                  // existing — REUSED for new widget
} client_info_t;
```

### New: `dev_cmd_state_t` (src/protocol.h or src/redis.h)

Transient state for development commands. Not persisted — derived from Redis poll.

```c
typedef struct {
    bool grid_enabled;
    int  grid_size;        // pixels; 0 = disabled
    bool textsize_enabled;
} dev_cmd_state_t;
```

This can be a global or part of the existing `g_registry` state — implementation detail.

### Unchanged Entities

| Entity | Notes |
|--------|-------|
| `gpu_info_t` | No changes |
| `disk_entry_t` | No changes |
| `activity_t` | No changes |
| `repo_entry_t` | No changes |

## State Transitions

### Client Widget Health State

```
        health key present         health key expired (TTL)
ONLINE ──────────────────→ ONLINE    ONLINE ──────────→ OFFLINE
  │                                    │
  │  center circle: green              │  center circle: red
  │  arcs: colored per metric          │  arcs: last known values (or gray)
  └────────────────────────────────────┘
```

### Development Command State

```
  Redis key absent           SET with TTL              Key TTL expires
INACTIVE ──────────→ INACTIVE  INACTIVE ──────────→ ACTIVE  ACTIVE ──────────→ INACTIVE
                                  │                           │
                                  │  overlay visible          │  overlay removed
                                  └───────────────────────────┘
```

### Disk Bar Color Tiers

```
usage_pct ≤ 60%   → fill color: green
60% < usage_pct ≤ 75%  → fill color: orange
usage_pct > 75%   → fill color: red
```

## Validation Rules

| Field | Rule | Source |
|-------|------|--------|
| `os_name` | May be empty string if client doesn't report it; display as blank | Health JSON |
| `disk_count` | Display min(disk_count, 6) bars | FR-020 |
| `grid_size` | Must be > 0; ignore command if ≤ 0 | Edge case |
| `cpu_pct`, `top_core_pct`, `gpu.compute_pct` | Clamp to 0–100 for arc value | Arc range |
| `ram_used_mb`, `gpu.vram_used_mb` | Clamp to 0–total for percentage calculation | Arc range |

## Relationships

```
client_info_t
├── 1:1 gpu_info_t (optional — gpu.present flag)
├── 1:N disk_entry_t (0..MAX_DISKS, display capped at 6)
└── 1:1 client widget (lv_obj_t *container)

dev_cmd_state_t
├── 1:0..1 grid overlay widget
└── 1:0..1 textsize reference widget
```
