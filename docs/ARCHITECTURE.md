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
| Client language | Python 3.13+ | psutil, pynvml, GitPython>=3.1 available on all platforms |
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
│  │  │  fortune · status_bar · dev_grid · dev_textsize │  │  │
│  │  │  dev_graph · redis_error_overlay                │  │  │
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
3. **Dev Telemetry**: client writes `kpidash:client:{h}:dev_telemetry` (JSON,
   EX 5 s) at 1 s intervals with GPU compute, CPU avg/top, VRAM, RAM for the
   dev graph. Only read when graph is enabled.
4. **Activities**: client writes HSET + ZADD; dashboard ZREVRANGE top-10 +
   HGETALL per activity. Active entries show live elapsed time via LVGL timer.
   Displayed as table: host | duration | DoW | activity name.
5. **Repos**: client writes HGETALL `kpidash:repos:{h}` (field=path,
   value=JSON, EX 30 s). Dashboard shows repos where branch ≠ default or
   is_dirty. Expanded fields include ahead/behind counts, untracked/changed/
   deleted/renamed counts, detached head, last commit timestamp.
6. **Fortune**: rotation timer in dashboard runs `fortune` popen every 300 s
   and caches in `kpidash:fortune:current`. Clients can push an override via
   `kpidash:fortune:pushed` with TTL.
7. **Status**: dashboard pushes warning/error messages to
   `kpidash:status:current`. Client CLIs read and acknowledge via
   `kpidash:status:ack:{id}`.
8. **Dev Commands**: `kpidash:cmd:grid`, `kpidash:cmd:textsize`,
   `kpidash:cmd:graph` control developer overlay widgets. Set via redis-cli
   with 300 s TTL. Auto-expire disables the overlay.

## Source Structure

```
kpidash/
├── CMakeLists.txt              # CMake 3.22+, hiredis, cJSON, libdrm, LVGL
├── lv_conf.h                   # LVGL configuration
├── fonts/
│   ├── generate.sh             # lv_font_conv: Montserrat Bold 14-48px + NF icons
│   ├── lv_font_custom.h        # Extern declarations for generated bold fonts
│   ├── lv_font_montserrat_bold_*.c  # Generated font sources (7 sizes)
│   └── ttf/                    # Source TTF files (Montserrat-Bold, SymbolsNerdFont)
├── src/
│   ├── main.c                  # Entry: config, LVGL init, DRM, poll timers
│   ├── config.{h,c}            # Environment variable parsing
│   ├── registry.{h,c}          # In-memory client state (mutex-protected)
│   ├── redis.{h,c}             # hiredis poll cycle + JSON parsing (cJSON)
│   ├── status.{h,c}            # In-memory status message FIFO queue
│   ├── fortune.{h,c}           # fortune popen + pushed-fortune override
│   ├── ui.{h,c}                # Screen layout + redis error overlay
│   ├── protocol.h              # Key macros, TTLs, capacity limits
│   └── widgets/
│       ├── client_card.{h,c}   # Per-client arc gauge card (CPU/RAM/GPU/disks)
│       ├── activities.{h,c}    # Activity table with live elapsed timers
│       ├── repo_status.{h,c}   # Repo card grid (branch/dirty/age indicators)
│       ├── fortune.{h,c}       # Fortune text label widget
│       ├── status_bar.{h,c}    # Bottom status bar (warning/error)
│       ├── dev_grid.{h,c}      # Pixel grid overlay (dev command)
│       ├── dev_textsize.{h,c}  # Font size reference panel (dev command)
│       └── dev_graph.{h,c}     # 5-series time-series chart (dev command)
├── tests/
│   ├── test_config.c           # Config env var parsing (ctest, no hardware)
│   └── test_redis_json.c       # cJSON parsing helpers (ctest, no hardware)
├── clients/
│   ├── kpidash-client/         # Python 3.13+ daemon + CLI (psutil, pynvml)
│   └── kpidash-mcp/            # Python 3.13+ MCP server (mcp>=1)
├── scripts/
│   └── load_test.py            # 8-client concurrent write stress test
└── docs/
    ├── ARCHITECTURE.md         # This file
    ├── CLIENT-PROTOCOL.md      # Redis schema canonical reference
    └── HANDOFF-CROSSCOMPILE.md # Cross-compilation guide for Pi 5
```

## Widget Layout (3840×2160)

```
┌──────────────────────────────────────────────────────────────┐
│  [dev_graph 2×1]  [client card] [client card] [client card] │
│  (when enabled)   [client card] [client card] [client card] │
├──────────────────────┬──────────────────────────────────────┤
│  Activities  (2×1)   │  Repo Status  (2×1)                  │
│  table: host|dur|    │  card grid: 4×4 repo cards with      │
│  DoW|activity name   │  branch/dirty/age indicators          │
├──────────────────────┴──────────────────────────────────────┤
│  Fortune widget (bottom strip)                               │
├──────────────────────────────────────────────────────────────┤
│  Status bar (hidden when no messages)                        │
└──────────────────────────────────────────────────────────────┘

Unit sizes (based on client card = 1×1 = 624×620):
  1×1 = 624 × 620    (client card, single widget)
  2×1 = 1256 × 620   (activities, repo status, dev graph)
  Gap = 8px between units
```

## Color Palette (Catppuccin Mocha)

| Name | Hex | Usage |
|------|-----|-------|
| Base | `#1E1E2E` | Widget backgrounds |
| Crust | `#11111B` | Screen background |
| Text | `#CDD6F4` | Primary text (white) |
| Overlay0 | `#6C7086` | Muted text (hosts, secondary) |
| Surface0 | `#313244` | Arc backgrounds |
| Surface1 | `#45475A` | Widget borders |
| Pink | `#F5C2E7` | Widget headers |
| Green | `#A6E3A1` | Active, disk OK, default branch |
| Blue | `#89B4FA` | Done, CPU avg |
| Peach | `#FAB387` | Hostname, GPU VRAM, warnings |
| Red | `#F38BA8` | Offline, disk critical, non-default branch, RAM |
| Mauve | `#CBA6F7` | GPU compute, non-default branch alt |
| Teal | `#94E2D5` | CPU top core |
| Yellow | `#F9E2AF` | Disk warning tier, commit age warning |

## Custom Font Pipeline

Bold fonts generated via `lv_font_conv` from Montserrat-Bold.ttf at sizes
14, 16, 20, 24, 28, 36, 48px. Includes ASCII (0x20-0x7E), Nerd Font Logos
(0xF300-0xF381), and Git icons (0xF1D2-0xF1D3). Regular-weight
`lv_font_montserrat_20` from LVGL built-in is used for activity text.

## Key Design Decisions

- **No title bar** (FR-002a): LVGL root screen uses flex layout, no title bar object.
- **No scroll** (FR-008): all containers have `LV_OBJ_FLAG_SCROLLABLE` cleared.
- **Offline detection via TTL**: health key has EX 5; if missing → offline.
- **Fortune compile-time constant**: `FORTUNE_INTERVAL_S 300` in `protocol.h`
  — not an env var — to avoid accidental very short/long intervals.
- **Priority eviction** (T056): `KPIDASH_PRIORITY_CLIENTS` comma list;
  priority clients never evicted when registry is full.


