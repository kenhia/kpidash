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
| Threading | **None** — `LV_USE_OS = LV_OS_NONE` | The process is single-threaded: the Redis poll and every draw task run on the one LVGL thread. Sprint 018 (WI #1798) removed LVGL's render thread, which was costing a futex round-trip per draw task — ~1.1 cores down to ~0.4. Anything slow added to the poll stalls rendering directly |
| Message bus | Redis 7.x | TTL-based expiry = implicit offline detection; atomic writes; cross-platform clients |
| JSON parsing (C) | cJSON | Tiny, single-file C lib; no dynamic allocation surprises |
| Redis client (C) | hiredis 1.2.0 | Official C client; `find_package(hiredis)` on Debian Trixie |
| Shared feed logic | libkdash (`kdash_core`) | kdashdata's pure-logic half — shared payload/threshold rules across the dashboards. Sprint 018 links it for the apartment-temperature band classifier. The I/O shell (`kdash`) is deliberately NOT linked: kpidash keeps its own hiredis layer |
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
   absence = TTL expired = client offline → LED turns red. Since sprint 022
   a member only gets a card once it has actually published — the set is
   append-only, so membership alone proves nothing — but once admitted it
   keeps its card, which is what makes the red LED an outage report rather
   than a host quietly disappearing. See CLIENT-PROTOCOL §1.
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
9. **Screenshot**: `kpidash:screenshot` (consumed via GETDEL) triggers an
   in-process `lv_snapshot_take` → BMP at `/tmp/kpidash-shot.bmp`. Use the
   `krpidss` CLI to fetch a PNG remotely (sprint 011).
10. **Host staleness**: the fleet deployers write `kdash:stale:{host}:{deployer}`
    (JSON, **no TTL**) when they skip an unreachable host, and `DEL` it after a
    verified sync. Dashboard SCANs `kdash:stale:*` in the same cycle as the
    services scan and renders one footer card per stale host. This is the only
    feed kpidash reads that it does not own — it is registered in kdashdata
    (CD-18) and shared with other consumers (sprint 017).

## Source Structure

```
kpidash/
├── CMakeLists.txt              # CMake 3.22+, hiredis, cJSON, libdrm, LVGL, kdash_core
├── lv_conf.h                   # LVGL config; LV_USE_OS=LV_OS_NONE (single-threaded render)
├── lib/                        # git submodules
│   ├── lvgl/                   # LVGL 9.2.2 — needed by a Pi/native build only
│   └── kdashdata/              # libkdash; the GATE needs this one (registry.c calls it)
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
│   ├── layout.h                # Cell-based unit system constants and macros
│   ├── protocol.h              # Key macros, TTLs, capacity limits
│   └── widgets/
│       ├── common.h            # Shared color palette and font constants (WS_*)
│       ├── client_card.{h,c}   # Per-client arc gauge card (CPU/RAM/GPU/disks)
│       ├── activities.{h,c}    # Activity table (disabled — WI #365)
│       ├── repo_status.{h,c}   # Repo card grid (disabled — WI #365)
│       ├── service_card.{h,c}  # Service status card + host staleness card
│       ├── apt_temps_card.{h,c}# Per-zone temp/humidity card (footer, WI #364)
│       ├── fortune.{h,c}       # Fortune text label widget
│       ├── status_bar.{h,c}    # Bottom status bar (warning/error)
│       ├── dev_grid.{h,c}      # Pixel grid overlay (dev command)
│       ├── dev_textsize.{h,c}  # Font size reference panel (dev command)
│       └── dev_graph.{h,c}     # 5-series time-series chart (dev command)
├── tests/                      # all ctest, no hardware unless noted
│   ├── test_config.c           # Config env var parsing
│   ├── test_redis_json.c       # cJSON parsing helpers
│   ├── test_layout.c           # Unit system macro arithmetic
│   ├── test_layout_pool.c      # Rows-2-3 widget placement
│   ├── test_graph_router.c     # Per-host graph series routing
│   ├── test_icon_registry.c    # Nerd-font glyph lookup
│   ├── test_apttemps_band.c    # Apt-temp band edges via libkdash (sprint 018)
│   ├── test_service_card.c     # Service state/colour, payload parsing, freshness windows
│   ├── test_stale_card.c       # Host staleness feed reader (sprint 017)
│   └── test_widget_leak.c      # Widget leak regression (needs LVGL)
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

The layout uses a cell-based unit system defined in `src/layout.h`.
Each cell internalizes half the visual gap via `CELL_PAD` — adjacent
widget content has `4+4 = 8px` visual gap with no separate gap constant.

### Unit System Constants

| Constant | Value | Description |
|----------|-------|-------------|
| `SCR_W` | 3840 | Screen width |
| `SCR_H` | 2160 | Screen height |
| `UNIT_W` | 632 | Cell width (gap-inclusive) |
| `UNIT_H` | 628 | Cell height (gap-inclusive) |
| `CELL_PAD` | 4 | Per-side widget inset within cell |
| `PAD_TOP` | 0 | Top screen margin |
| `PAD_LEFT` | 24 | Left screen margin |
| `PAD_RIGHT` | 24 | Right screen margin |
| `FOOTER_H` | 276 | Footer height below 3 unit rows |
| `COLS` | 6 | Number of columns |
| `ROWS` | 3 | Number of unit-height rows |

### Macros

- `UNIT_W_N(n)` = `n × UNIT_W` (no gap correction needed)
- `UNIT_H_N(n)` = `n × UNIT_H`
- `ROW_Y(r)` = `PAD_TOP + r × UNIT_H`
- `COL_X(c)` = `PAD_LEFT + c × UNIT_W`

### Layout Arithmetic

```
Horizontal: PAD_LEFT + 6×UNIT_W + PAD_RIGHT = 24 + 3792 + 24 = 3840
Vertical:   PAD_TOP + 3×UNIT_H + FOOTER_H  = 0 + 1884 + 276 = 2160
Widget content (1×1): UNIT_W - 2×CELL_PAD = 624, UNIT_H - 2×CELL_PAD = 620
```

### Grid Layout

```
┌──────────────────────────────────────────────────────────────┐
│  Row 0: [card] [card] [card] [card] [card] [card]  (6×1)    │
├──────────────┬──────────────┬────────────────────────────────┤
│  Row 1:      │  Activities  │  Repo Status                   │
│  dev_graph   │  (2×1)       │  (2×1)                         │
│  (2×1, opt)  │              │                                │
├──────────────┴──────────────┴────────────────────────────────┤
│  Row 2: (available for future widgets)                       │
├──────────────────────────────────────────────────────────────┤
│  Footer (276px): Fortune strip + status bar                  │
└──────────────────────────────────────────────────────────────┘
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
14, 16, 20, 24, 28, 36, 48px. Includes ASCII (0x20-0x7E), Latin-1 Supplement
(0xA0-0xFF), the punctuation publishers actually type (en/em dash, curly
quotes, bullet, ellipsis), Nerd Font Logos (0xF300-0xF381), and Git icons
(0xF1D2-0xF1D3). Regular-weight `lv_font_montserrat_20` from LVGL built-in is
used for activity text.

The glyph set is declared once, in `RANGE` in `fonts/generate.sh`; it is
stated as a contract in `docs/CLIENT-PROTOCOL.md` §8a, and `just check-fonts`
asserts the committed `fonts/*.c` actually carry it. A character outside the
set draws as an empty box — there is no fallback font. Since sprint 022 the
resulting LVGL warning is deduped per codepoint by `src/logfilter.c`, which
is registered as LVGL's print callback in `main.c`; `lv_conf.h` therefore has
`LV_LOG_PRINTF 0`, because LVGL runs its own printf path *and* the callback.

## Key Design Decisions

- **No title bar** (FR-002a): LVGL root screen uses flex layout, no title bar object.
- **No scroll** (FR-008): all containers have `LV_OBJ_FLAG_SCROLLABLE` cleared.
- **Offline detection via TTL**: health key has EX 5; if missing → offline.
- **Fortune compile-time constant**: `FORTUNE_INTERVAL_S 300` in `protocol.h`
  — not an env var — to avoid accidental very short/long intervals.
- **Priority eviction** (T056): `KPIDASH_PRIORITY_CLIENTS` comma list;
  priority clients never evicted when registry is full.

## Sprint 006 — Service Cards & Per-Host Graph Router

- **Service registry** (`src/registry.c`): `g_services[SERVICE_REGISTRY_MAX=32]`
  pthread-mutex protected. Dashboard SCANs `kpidash:services:*` each cycle,
  parses each value (cJSON), and routes into the registry. Card border colour
  is computed by `service_color()` per the truth table in
  `sprints/006-layout-refresh-status-cards/data-model.md`: `DOWN`/`UNKNOWN` →
  GRAY (sticky for DOWN); other states → state colour iff
  `(now − ts) < 60 s`, else RED.
- **Service cards** (`src/widgets/service_card.c`): 220×240 px, 6 px coloured
  border, optional icon (lv_font_icons_56), name + status text. Rendered into
  the footer strip; one card per Redis key.
- **Per-host graph router** (`src/registry.c`,
  `src/widgets/dev_graph.c`): `dev_telemetry` samples carry a `host` field
  (default `"(legacy)"`). `graph_host_find_or_create()` returns a stable
  pointer; entries are touched on each sample and never evicted
  (`GRAPH_HOST_MAX=8`). Series go stale after
  `GRAPH_HOST_STALE_SECONDS=30.0` and the dev_graph widget overlays a
  "NO NEW DATA" banner via `dev_graph_set_stale()`. The current UI renders a
  single dev_graph for the most-recently-seen host; full multi-host expansion
  (T035) is deferred to a follow-up sprint.

## Sprint 017 — Host Staleness Cards

- **Staleness registry** (`src/registry.c`): `g_stale[STALE_REGISTRY_MAX=8]`,
  one entry per stale *host*, each holding up to `STALE_DEPLOYERS_MAX=8`
  deployer names. Unlike the service and apt-temps registries it is **rebuilt
  from the feed every poll**, because the feed is presence-owned: an entry not
  observed must go. Observations land in a pending set and only replace the
  live set on commit, so a scan that fails part-way can `abort` and leave the
  cards standing.
- **The unparseable-record rule is inverted here, deliberately.** Every other
  reader in `redis.c` drops a record it cannot parse. For this feed the *key*
  asserts staleness and the payload only describes it, so dropping a record
  would erase the flag — and a host with no card reads as healthy. An
  unreadable payload, an ISO-8601 `since`, or an off-contract key all still
  raise the host; only a key naming no host at all is ignored, and only a key
  deleted between the SCAN and the GET counts as cleared. See kdashdata CD-18
  and `docs/CLIENT-PROTOCOL.md` §8c.
- **Staleness cards** (`src/widgets/service_card.c`): built in the same module
  as the service card so the 220×240 footprint and the warn colour band are
  shared by construction rather than by a second set of constants. No icon, a
  wrapping title (the panel has no input devices, so an elided title can never
  be revealed), and a fixed warn border — there is no freshness computation to
  do, which is why `stale_card_update()` takes no `now`.
- **Footer strip order** is `[services][stale, host-sorted][apt-temps,
  slug-sorted]`, per Ken's ruling from the panel. Because the strip is a flex
  row whose child order is *creation* order, each of the two trailing groups
  walks its sorted snapshot moving every card to last — otherwise a host going
  stale long after boot would append at the end of the row.

## Memory Telemetry

The dashboard self-reports process and LVGL heap usage every 60 s via
`src/memstat.{h,c}` (spec 005-fix-memory-leaks). On startup `main.c`
calls `memstat_init()`, takes one immediate sample, then arms an
`lv_timer_create(memstat_timer_cb, 60000, NULL)`.

Each sample:

1. Reads `/proc/self/statm` for VSize and RSS in bytes
   (multiplied by `sysconf(_SC_PAGESIZE)`).
2. Calls `lv_mem_monitor()` for LVGL heap totals
   (total, free, used, max_used, frag_pct, free_biggest).
3. Logs one `memstat:` prefixed line to stdout for tail-based soak analysis.
4. Writes the sample to Redis:
   - `SET kpidash:system:mem:current <json>` — latest sample only.
   - `LPUSH kpidash:system:mem:ring <json>` + `LTRIM 0, 1498`
     — bounded ring buffer (~25 hours at 60 s cadence).
5. If `lvgl_used > 8 MiB` (high-water threshold), emits a
   `memstat: WARN` line to stderr and pushes a `STATUS_WARNING` to
   the in-process status FIFO, rate-limited to one warning per 300 s.

See [data-model.md](../sprints/005-fix-memory-leaks/data-model.md) for
the `mem_sample` schema and
[contracts/redis-keys.md](../sprints/005-fix-memory-leaks/contracts/redis-keys.md)
for the on-the-wire key contract. Client CLIs may consume these keys
diagnostically but they are not part of the supported public API.


