# KPI Dashboard — Architecture

## Overview

A fullscreen dashboard running on a Raspberry Pi 5 that displays real-time
status for multiple client machines. Clients write telemetry, health pings,
activity updates, and repo status to a local Redis instance. The dashboard
polls Redis every second and renders a live multi-widget display via
LVGL/DRM/KMS with no mouse or keyboard interaction.

## Goals

- **Redis as message bus**: decouples dashboard from clients; clients can be
  written in any language.
- **Multi-tenant**: up to 16 client machines tracked simultaneously.
- **AI-native**: an MCP server exposes `start_activity`/`end_activity` tools
  so AI agents can self-report their work on the dashboard.
- **Extensible**: new widgets and metrics are added by extending the Redis
  schema without changing the client wire format.

## Hardware / Environment

| Item | Detail |
|------|--------|
| Board | Raspberry Pi 5 |
| OS | Debian 13 (Trixie), kernel 6.6 |
| Display | HDMI-A-1, 3840×2160 @ 30 Hz |
| Graphics | DRM/KMS via `vc4-kms-v3d` overlay |
| DRI device | `/dev/dri/card1` (default, `KPIDASH_DRM_DEV` overrides) |
| Message bus | Redis 7.x, localhost, default port 6379 |

## Technology Choices

| Component | Choice | Rationale |
|-----------|--------|-----------|
| Dashboard language | C (C11) | LVGL is C-native; direct DRM access without a runtime |
| UI toolkit | LVGL 9.2.2 | Lightweight, embedded-friendly, excellent Pi support |
| Display backend | LVGL `lv_linux_drm` driver | Direct DRM/KMS, no X11/Wayland needed |
| Message bus | Redis 7.x | TTL-based expiry = implicit offline detection; atomic writes; cross-platform clients |
| JSON parsing (C) | cJSON | Tiny, single-file C lib; no dynamic allocation surprises |
| Redis client (C) | hiredis 1.2.0 | Official C client; `find_package(hiredis)` on Debian Trixie |
| Client language | Python 3.11+ | psutil, pynvml, GitPython available on all platforms |
| Client Redis | redis-py 5+ | Official Python client with pipeline support |
| MCP server | Python + `mcp>=1` | FastMCP for tool registration; stdio transport |
| Build system | CMake 3.22+ | LVGL ships CMakeLists; standard for C projects |
| Cross-compile | aarch64 GCC toolchain | x86_64 host → Pi 5 target |

## High-Level Component Diagram

```
┌──────────────────────────────────────────────────────────────┐
│                     Raspberry Pi 5                           │
│                                                              │
│  ┌────────────────────────────────────────────────────────┐  │
│  │                   kpidash (C binary)                   │  │
│  │                                                        │  │
│  │  ┌───────────┐  1s timer  ┌────────────────────────┐  │  │
│  │  │ main.c    │──────────>│ redis.c (poll cycle)    │  │  │
│  │  │ LVGL loop │           │  SMEMBERS / GET / HGETALL │  │  │
│  │  └───────────┘           └──────────┬─────────────┘  │  │
│  │                                     │                 │  │
│  │  ┌──────────────────────────────────▼──────────────┐  │  │
│  │  │ registry.c (in-memory client state)             │  │  │
│  │  └──────────────────────────────────┬──────────────┘  │  │
│  │                                     │                 │  │
│  │  ┌──────────────────────────────────▼──────────────┐  │  │
│  │  │ ui.c + widgets/ (LVGL screen composition)       │  │  │
│  │  │  client_card · activities · repo_status         │  │  │
│  │  │  fortune · status_bar · redis_error_overlay     │  │  │
│  │  └──────────────────────────────────┬──────────────┘  │  │
│  │                                     │                 │  │
│  └─────────────────────────────────────┼─────────────────┘  │
│                                        │                     │
│                              ┌─────────▼─────────┐          │
│                              │  DRM/KMS display  │          │
│                              │  (fullscreen)     │          │
│                              └───────────────────┘          │
│                                                              │
│  ┌────────────────────────────┐                              │
│  │     Redis 7.x (localhost)  │                              │
│  └────────────────────────────┘                              │
└──────────────────────────────────────────────────────────────┘
             ▲ Redis writes (health/telemetry/activity/repos)
             │
┌────────────┴──────────────────────────────────────────────┐
│              Remote machines (Linux or Windows)           │
│                                                           │
│  ┌───────────────────────────────────────────────────┐    │
│  │  kpidash-client (Python daemon)                   │    │
│  │  • write_health()   every ~3 s (EX 5)            │    │
│  │  • write_telemetry() every ~5 s (EX 15)          │    │
│  │  • write_repos()    every ~30 s (EX 30)          │    │
│  │  CLI: activity start|done, fortune push,          │    │
│  │       status ack, log-path, daemon start|stop     │    │
│  └───────────────────────────────────────────────────┘    │
│                                                           │
│  ┌───────────────────────────────────────────────────┐    │
│  │  kpidash-mcp (Python MCP server)                  │    │
│  │  Tools: start_activity, end_activity              │    │
│  │  Consumers: Claude Desktop, GitHub Copilot        │    │
│  └───────────────────────────────────────────────────┘    │
└───────────────────────────────────────────────────────────┘
```

## Data Flow

1. **Health**: client writes `kpidash:client:{h}:health` (JSON, EX 5 s).
   Dashboard SMEMBERS the registry set then GETs each health key. Key
   absence = TTL expired = client offline → LED turns red.
2. **Telemetry**: client writes `kpidash:client:{h}:telemetry` (JSON, EX 15 s)
   with CPU, RAM, GPU, disk data. Dashboard parses and updates client cards.
3. **Activities**: client writes HSET + ZADD; dashboard ZREVRANGE top-10 +
   HGETALL per activity. Active entries show live elapsed time via LVGL timer.
4. **Repos**: client writes HGETALL `kpidash:repos:{h}` (field=path,
   value=JSON, EX 30 s). Dashboard shows repos where branch ≠ default or
   is_dirty.
5. **Fortune**: rotation timer in dashboard runs `fortune` popen every 300 s
   and caches in `kpidash:fortune:current`. Clients can push an override via
   `kpidash:fortune:pushed` with TTL.
6. **Status**: dashboard pushes warning/error messages to
   `kpidash:status:current`. Client CLIs read and acknowledge via
   `kpidash:status:ack:{id}`.

## Source Structure

```
kpidash/
├── CMakeLists.txt              # CMake 3.22+, hiredis, cJSON, libdrm, LVGL
├── lv_conf.h                   # LVGL configuration
├── src/
│   ├── main.c                  # Entry: config, LVGL init, DRM, poll timers
│   ├── config.{h,c}            # Environment variable parsing
│   ├── registry.{h,c}          # In-memory client state (mutex-protected)
│   ├── redis.{h,c}             # hiredis poll cycle + JSON parsing (cJSON)
│   ├── status.{h,c}            # In-memory status message FIFO queue
│   ├── fortune.{h,c}           # fortune popen + pushed-fortune override
│   ├── ui.{h,c}                # Screen layout + redis error overlay
│   └── widgets/
│       ├── client_card.{h,c}   # Per-client health/telemetry card
│       ├── activities.{h,c}    # Activity list with live elapsed timers
│       ├── repo_status.{h,c}   # Repo dirty/branch status list
│       ├── fortune.{h,c}       # Fortune text label widget
│       └── status_bar.{h,c}    # Bottom status bar (warning/error)
├── tests/
│   ├── test_config.c           # Config env var parsing (ctest, no hardware)
│   └── test_redis_json.c       # cJSON parsing helpers (ctest, no hardware)
├── clients/
│   ├── kpidash-client/         # Python 3.11+ daemon + CLI (psutil, pynvml)
│   └── kpidash-mcp/            # Python 3.11+ MCP server (mcp>=1)
├── scripts/
│   └── load_test.py            # 8-client concurrent write stress test
└── docs/
    ├── ARCHITECTURE.md         # This file
    ├── CLIENT-PROTOCOL.md      # Redis schema canonical reference
    └── HANDOFF-CROSSCOMPILE.md # Cross-compilation guide for Pi 5
```

## Key Design Decisions

- **No title bar** (FR-002a): LVGL root screen uses flex layout, no title bar object.
- **No scroll** (FR-008): all containers have `LV_OBJ_FLAG_SCROLLABLE` cleared.
- **Offline detection via TTL**: health key has EX 5; if missing → offline.
- **Fortune compile-time constant**: `FORTUNE_INTERVAL_S 300` in `protocol.h`
  — not an env var — to avoid accidental very short/long intervals.
- **Priority eviction** (T056): `KPIDASH_PRIORITY_CLIENTS` comma list;
  priority clients never evicted when registry is full.


## High-Level Components

```
┌─────────────────────────────────────────────────┐
│                Raspberry Pi 5                   │
│                                                 │
│  ┌──────────────┐    ┌────────────────────────┐ │
│  │ UDP Listener │───>│ Client Registry        │ │
│  │ (thread)     │    │ (array of client_info) │ │
│  └──────────────┘    └────────┬───────────────┘ │
│                               │                 │
│                    ┌──────────▼──────────┐      │
│                    │ LVGL UI             │      │
│                    │ - per-client widget │      │
│                    │ - status LED        │      │
│                    │ - task + elapsed    │      │
│                    └──────────┬──────────┘      │
│                               │                 │
│                    ┌──────────▼──────────┐      │
│                    │ DRM/KMS Display     │      │
│                    │ (fullscreen)        │      │
│                    └─────────────────────┘      │
└─────────────────────────────────────────────────┘

         ▲  UDP :5555
         │
    ┌────┴────────────────────┐
    │  Client machines        │
    │  (any OS / language)    │
    │  - health ping thread   │
    │  - task update commands  │
    └─────────────────────────┘
```

## Data Model

```c
#define MAX_CLIENTS 16
#define HOSTNAME_LEN 64
#define TASK_LEN 128

typedef struct {
    char hostname[HOSTNAME_LEN];
    time_t last_health;        // epoch of last health ping
    double uptime;             // seconds, from client
    char task[TASK_LEN];       // current task description
    time_t task_start;         // epoch when task started
    bool active;               // slot in use

    // LVGL widget handles
    lv_obj_t *container;
    lv_obj_t *status_led;
    lv_obj_t *hostname_label;
    lv_obj_t *task_label;
    lv_obj_t *elapsed_label;
} client_info_t;
```

## Threading Model

| Thread | Responsibility |
|--------|---------------|
| Main thread | LVGL event loop (`lv_timer_handler`), display refresh |
| UDP thread | `recvfrom()` loop, parse JSON, update `client_info_t` with mutex |

A `pthread_mutex_t` protects the client registry. The UDP thread locks
briefly to write, the LVGL timer callback locks briefly to read.

## UI Layout (POC)

```
┌──────────────────────────────────────────────┐
│  KPI Dashboard                     12:34:56  │
├──────────────────────────────────────────────┤
│                                              │
│  ┌─────────────────────────────────────────┐ │
│  │ 🟢  kubs0                               │ │
│  │     kris development          00:42:15  │ │
│  └─────────────────────────────────────────┘ │
│                                              │
│  ┌─────────────────────────────────────────┐ │
│  │ 🔴  devbox1                             │ │
│  │     (no task)                  --:--:--  │ │
│  └─────────────────────────────────────────┘ │
│                                              │
│  ┌─────────────────────────────────────────┐ │
│  │ 🟢  laptop3                             │ │
│  │     api refactor              01:03:47  │ │
│  └─────────────────────────────────────────┘ │
│                                              │
└──────────────────────────────────────────────┘
```

- Title bar with dashboard name and clock.
- Scrollable list of client cards.
- Each card: colored LED, hostname, task description, elapsed time.
- LED turns red when no health ping received for `HEALTH_TIMEOUT` seconds (default 10).
- New clients auto-appear when first message arrives.

## Directory Structure (Planned)

```
kpidash/
├── CMakeLists.txt
├── lv_conf.h                  # LVGL configuration
├── src/
│   ├── main.c                 # init display, start threads, run loop
│   ├── ui.c / ui.h            # LVGL widget creation & update
│   ├── net.c / net.h          # UDP listener thread
│   ├── registry.c / registry.h # client registry + mutex
│   └── protocol.h             # message type constants
├── lib/
│   ├── lvgl/                  # LVGL as git submodule
│   └── lv_drivers/            # LVGL Linux drivers (submodule or vendored)
├── docs/
│   ├── ARCHITECTURE.md        # this file
│   ├── IMPLEMENTATION-PLAN.md
│   └── CLIENT-PROTOCOL.md    # protocol spec for client authors
└── .gitignore
```

## Future Considerations (Out of POC Scope)

- REST API alongside or replacing UDP.
- TLS / DTLS for encrypted transport.
- Authentication tokens per client.
- Persistent client list (SQLite or flat file).
- Touch interaction (tap client card for details).
- Theming / branding.
- Configurable layout (grid vs. list, multi-page).
- Systemd service for auto-start.
