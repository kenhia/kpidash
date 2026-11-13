# KPI Dashboard — Architecture

## Overview

A fullscreen dashboard running on a Raspberry Pi 5 that displays real-time
status for multiple client machines. Clients write telemetry, health pings
and card feeds (network, NAS, services) to a local Redis instance. The dashboard
polls Redis every second and renders a live multi-widget display via
LVGL/DRM/KMS with no mouse or keyboard interaction.

## Goals

- **Redis as message bus**: decouples dashboard from clients; clients can be
  written in any language.
- **Multi-tenant**: up to 32 client machines tracked, 30 of them on screen at
  once (sprint 029).
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
│  │  │  host_columns · net_card · nas_card             │  │  │
│  │  │  service_card · status_bar · dev_grid           │  │  │
│  │  │  dev_textsize · redis_error_overlay             │  │  │
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
   absence = TTL expired = client offline. Since sprint 035 (WI #4592) a
   column is held on the host's `last_seen_ts`: for the first 30 s nothing on
   screen changes, then it turns red in place and reads `NO DATA <age>`, and
   after 5 min a host that declared itself intermittent gives up its slot (an
   always-on host stays red). Since sprint 022 a member only gets a slot
   once it has actually published — the set is append-only, so membership
   alone proves nothing — but once admitted it keeps its slot, which is what
   makes the red border an outage report rather than a host quietly
   disappearing. The admitted set is persisted to
   `/var/lib/kpidash/admitted` (`KPIDASH_STATE_FILE`) and loaded at startup,
   so the admission outlives the process and a host that is down across a
   restart still shows red rather than vanishing. Dashboard-local state, not
   part of the Redis contract. See CLIENT-PROTOCOL §1.
2. **Telemetry**: client writes `kpidash:client:{h}:telemetry` (JSON, EX 15 s)
   with CPU, RAM, GPU, disk data. Dashboard parses it into the registry and,
   while the key is present, records CPU average, busiest core and GPU
   compute into a 10-minute in-memory ring per host (`hosthist.c`, sprint
   029) for the host columns' sparklines.
3. **Dev Telemetry**: client writes `kpidash:client:{h}:dev_telemetry` (JSON,
   EX 5 s) at 1 s intervals with GPU compute, CPU avg/top, VRAM, RAM. The
   dashboard reads none of it since sprint 033: the per-host dev graphs went in
   032, and the NAS card's activity chart, the last reader, in 033. kdeskdash
   still reads it, so the producer stays.
4. **Activities, repo status, Fortune**: removed in sprint 032; see
   [Removed widgets](#removed-widgets). The dashboard reads none of their keys.
6a. **Network health** (sprint 024): each Linux client writes
   `kdash:net:{host}` (kdashdata CD-27) every ~5 s, EX 15; the dashboard MGETs
   the configured hosts each poll into `netstate.c` (pure model) and draws
   `widgets/net_card.c` in row 1, columns 0-1. The same MGET
   reads each host's `kdash:net:ack:{host}` (sprint 027), which the panel
   applies to clear a reviewed warning; hosts that have ever published are
   persisted as the `[net]` section of the admitted-hosts file, so they read
   STALE rather than no feed after a restart. A failed Internet probe is
   drawn only after 3 consecutive failed records from that host (sprint 035,
   WI #4542): one lost echo is noise. See CLIENT-PROTOCOL.md §8d.
7. **Status**: dashboard pushes warning/error messages to
   `kpidash:status:current`. Client CLIs read and acknowledge via
   `kpidash:status:ack:{id}`.
8. **Dev Commands**: `kpidash:cmd:grid` and `kpidash:cmd:textsize` control
   developer overlay widgets. Set via redis-cli
   with 300 s TTL. Auto-expire disables the overlay.
9. **Screenshot**: `kpidash:screenshot` (consumed via GETDEL) triggers an
   in-process `lv_snapshot_take` → BMP at `/tmp/kpidash-shot.bmp`. Use the
   `krpidss` CLI to fetch a PNG remotely (sprint 011).
10. **Host staleness**: the fleet deployers write `kdash:stale:{host}:{deployer}`
    (JSON, **no TTL**) when they skip an unreachable host, and `DEL` it after a
    verified sync. Dashboard SCANs `kdash:stale:*` in the same cycle as the
    services scan and renders one footer card per stale host. Like `kdash:net`
    and `kdash:update`, it is registered in kdashdata (CD-18) and shared with
    other consumers rather than owned here (sprint 017).
11. **Update issues**: k-homelab's apt runner writes `kdash:update:{host}:{kind}`
    (JSON, **no TTL**, presence-owned like staleness) when a run needs Ken — a
    held package, a pending reboot, a stopped ring. Dashboard SCANs
    `kdash:update:*` in the same cycle and renders one footer card per host,
    after the staleness cards, with each payload's `issue` line as the body.
    rpi53 is included. See CLIENT-PROTOCOL.md §8e (sprint 028).
12. **NAS big panel**: kpidash-client on gratch projects gratch-ui's collector
    file into `kdash:nas:gratch` (EX 15, kdashdata v1). Dashboard GETs it each
    poll into `nasstate.c` (pure model) and, once it has published, draws
    `widgets/nas_card.c` in row 1, columns 2-3 — a state headline, pool
    and root bars, eight disk tiles labelled by use (sprint 033, "A · Rail"
    in mauve). See CLIENT-PROTOCOL.md §8f (sprint 028).
13. **Weather filler**: kweather on kai writes `kdash:weather:home` about every
    10 minutes (kdashdata v1, **no TTL**, ts-owned). Dashboard GETs it each
    poll into `wxstate.c` (pure model; an unchanged value is one strcmp) and
    draws `widgets/weather_card.c` in the host row's spare columns. Read only:
    kweather owns the key. See CLIENT-PROTOCOL.md §8g (sprint 034).

## Source Structure

```
kpidash/
├── CMakeLists.txt              # CMake 3.22+, hiredis, cJSON, libdrm, LVGL, kdash_core
├── lv_conf.h                   # LVGL config; LV_USE_OS=LV_OS_NONE (single-threaded render)
├── lib/                        # git submodules
│   ├── lvgl/                   # LVGL 9.2.2 — needed by a Pi/native build only
│   └── kdashdata/              # libkdash; the GATE needs this one (registry.c calls it)
├── fonts/
│   ├── generate.sh             # lv_font_conv: Montserrat Bold 14-48px, 96px digits, NF icons
│   ├── lv_font_custom.h        # Extern declarations for generated bold fonts
│   ├── lv_font_montserrat_bold_*.c  # Generated font sources (7 sizes + 96px digits)
│   └── ttf/                    # Source TTF files (Montserrat-Bold, SymbolsNerdFont)
├── src/
│   ├── main.c                  # Entry: config, LVGL init, DRM, poll timers
│   ├── config.{h,c}            # Environment variable parsing
│   ├── registry.{h,c}          # In-memory client state (mutex-protected)
│   ├── hosthist.{h,c}          # Per-host 10-min CPU/top-core/GPU ring (sprint 029)
│   ├── hostcols.{h,c}          # Host columns fill rule, order, attention (pure)
│   ├── wxstate.{h,c}           # Weather filler: parse, trust, space, scale (pure, sprint 034)
│   ├── redis.{h,c}             # hiredis poll cycle + JSON parsing (cJSON)
│   ├── status.{h,c}            # In-memory status message FIFO queue
│   ├── ui.{h,c}                # Screen layout + redis error overlay
│   ├── layout.h                # Cell-based unit system constants and macros
│   ├── protocol.h              # Key macros, TTLs, capacity limits
│   └── widgets/
│       ├── common.h            # Shared color palette and font constants (WS_*)
│       ├── host_columns.{h,c}  # Row 0 host columns / compact tiles (sprint 029)
│       ├── net_card.{h,c}      # 10G network-health card (row 1, sprint 024)
│       ├── nas_card.{h,c}      # NAS big panel (row 1, sprint 028; restyled 033)
│       ├── weather_card.{h,c}  # Weather filler in the host row's spare columns (034)
│       ├── service_card.{h,c}  # Service status card + host staleness card
│       ├── apt_temps_card.{h,c}# Per-zone temp/humidity card (footer, WI #364)
│       ├── status_bar.{h,c}    # Bottom status bar (warning/error)
│       ├── dev_grid.{h,c}      # Pixel grid overlay (dev command)
│       └── dev_textsize.{h,c}  # Font size reference panel (dev command)
├── tests/                      # all ctest, no hardware unless noted
│   ├── test_config.c           # Config env var parsing
│   ├── test_redis_json.c       # cJSON parsing helpers
│   ├── test_layout.c           # Unit system macro arithmetic
│   ├── test_icon_registry.c    # Nerd-font glyph lookup
│   ├── test_apttemps_band.c    # Apt-temp band edges via libkdash (sprint 018)
│   ├── test_service_card.c     # Service state/colour, payload parsing, freshness windows
│   ├── test_stale_card.c       # Host staleness feed reader (sprint 017)
│   ├── test_hosthist.c         # History ring: wrap, gaps, eviction (sprint 029)
│   ├── test_hostcols.c         # Fill-rule table, host order, attention levels
│   ├── test_nasstate.c         # NAS model against the kdash-nas schema (sprint 028)
│   ├── test_wxstate.c          # Weather model against the kdash-weather schema (034)
│   ├── test_widget_leak.c      # Widget leak regression (needs LVGL)
│   ├── render_host_columns.c   # Offline render of the host columns to BMP (tool)
│   ├── render_nas_card.c       # Offline render of the NAS card, four states (tool)
│   └── render_weather.c        # Offline render of the weather filler, 4 states × 4 widths (tool)
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
│  Row 0: [ host columns (6×1) | weather in what they leave ]  │
├──────────────┬──────────────┬────────────────────────────────┤
│  Row 1:      │  Row 1:      │  (free)                        │
│  10G card    │  NAS card    │                                │
│  (2×1)       │  (2×1)       │                                │
├──────────────┴──────────────┴────────────────────────────────┤
│  Row 2: (free)                                               │
├──────────────────────────────────────────────────────────────┤
│  Footer (276px): service strip + status bar                  │
└──────────────────────────────────────────────────────────────┘
```

Both row-1 cards have fixed slots (sprint 032): the NAS card is hidden until
`kdash:nas` has published, but its slot stays reserved, so the 10G card never
moves. What fills the free cells is a later decision.

## Removed widgets

Removed in sprint 032 (korg:4562), code and all. Everything below is
retrievable from the git tag **`last-with-dev-graphs-fortune`**
(`git checkout last-with-dev-graphs-fortune -- <paths>`).

| Widget | What it showed | Paths at the tag |
|---|---|---|
| Dev graphs | Per-host 2×1 time-series chart of CPU avg/top core, RAM, GPU compute/VRAM from `dev_telemetry`; one per host, placed by the rows-1-2 layout pool | `src/widgets/dev_graph.{h,c}`, `layout_pool_place()` in `src/registry.c`, `tests/test_layout_pool.c` |
| Fortune | A rotating `fortune` quote, with a client push override (`kpidash:fortune:pushed`); compiled out since sprint 024 | `src/fortune.{h,c}`, `src/widgets/fortune.{h,c}` |
| Activities | Table of agent/CLI activities with live elapsed timers (`kpidash:activities`); compiled out since sprint 012 (WI #365) | `src/widgets/activities.{h,c}`, `poll_activities()` in `src/redis.c` |
| Repo Status | Card grid of repos off their default branch or dirty (`kpidash:repos:{host}`); compiled out since sprint 012 (WI #365) | `src/widgets/repo_status.{h,c}`, `poll_repos()` in `src/redis.c` |

The host columns (sprints 029-031) replaced the dev graphs. The producers of
the other three feeds still publish, and nothing reads them: see
`docs/CLIENT-PROTOCOL.md`.

## Color Palette (Catppuccin Mocha)

| Name | Hex | Usage |
|------|-----|-------|
| Base | `#1E1E2E` | Widget backgrounds |
| Crust | `#11111B` | Screen background |
| Text | `#CDD6F4` | Primary text (white) |
| Overlay0 | `#6C7086` | Muted text (hosts, secondary) |
| Surface0 | `#313244` | Bar tracks; host column fill at 26 % |
| Surface1 | `#45475A` | Widget borders |
| Pink | `#F5C2E7` | Widget headers |
| Green | `#A6E3A1` | Active, disk OK, default branch |
| Blue | `#89B4FA` | Done, CPU avg |
| Peach | `#FAB387` | GPU VRAM bar and figures, warnings |
| Red | `#F38BA8` | Offline, critical attention, non-default branch |
| Mauve | `#CBA6F7` | RAM bar and figure, non-default branch alt |
| Teal | `#94E2D5` | 10G network fabric |
| Top core | `#AB47BC` | Host columns' top core (Ken's pick, not Mocha) |
| Yellow | `#F9E2AF` | Disk warning tier, commit age warning |

## Custom Font Pipeline

Bold fonts generated via `lv_font_conv` from Montserrat-Bold.ttf at sizes
14, 16, 20, 24, 28, 36, 48px. Includes ASCII (0x20-0x7E), Latin-1 Supplement
(0xA0-0xFF), the punctuation publishers actually type (en/em dash, curly
quotes, bullet, ellipsis), Nerd Font Logos (0xF300-0xF381), and Git icons
(0xF1D2-0xF1D3). Regular-weight `lv_font_montserrat_20` from LVGL built-in is
used for the host columns' labels.

`lv_font_montserrat_bold_96_digits` (sprint 034) is the weather filler's
temperature: 0-9, minus, degree and em dash only, so it is checked for exactly
those by `just check-fonts` rather than against `RANGE`.

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

## Host Columns (sprints 029, 030)

Row 0, all six cells: one 3792×628 widget (`src/widgets/host_columns.c`) that
replaced the six 1×1 client cards (029) and was widened to the whole row with
the text roughly doubled in 030, because 14 px was unreadable from Ken's chair.
The columns to the right of the last host take the weather filler (sprint
034, below). Spec: `sprints/planning/hosts-wide-mockups.html` (`plan`, `colFull`,
`tile`, `widget`); the 029 spec was `hosts-columns-mockups.html`. Nothing on
the widget is smaller than 20 px.

- **Full column** (361×596): band; hostname (28 bold); uptime with the OS name
  muted on the same line, shortened to name plus a two-part version
  (`hostcols_short_os`, sprint 031: "Ubuntu 24.04.5 LTS" → "Ubuntu 24.04");
  CPU on one line — average (36 bold, blue) / top core
  (28 bold, `WS_COLOR_TOP_CORE` #AB47BC); a 321×130 sparkline with the average
  filled blue and the busiest core as a purple line over it; RAM figure and bar
  in mauve; GPU figure, 321×70 sparkline, and VRAM figures and bar in peach when
  the host has a GPU; one row for the **fullest drive**, with `+N` when N other
  drives are also over the warn line, pinned to the bottom. Every row sits at
  the same y in every column, so a host without a GPU leaves a gap.
- **Compact tile** (361×189, three to a column): dot, hostname, uptime on the
  same line, and one bar each for cpu (with a purple top-core tick), ram, gpu
  and the fullest drive.
- **Fill rule** (`hostcols_plan`): n ≤ 10 → all full; otherwise
  `full = (30 − n) / 2` and the rest are tiles. At most 30 hosts; past that
  the lowest priorities are dropped and the count is logged.
- **Order** = demotion order: `KPIDASH_PRIORITY_CLIENTS`, then hostname.
- **Attention is colour only** — nothing grows. Yellow (warn) or red (crit)
  3 px border and band, the offending row in the same colour. RAM and every
  drive (all of them, not only the one drawn) warn at ≥ 80 % and are critical at ≥ 90 %. CPU, GPU and VRAM never
  raise attention. A host that stops reporting is held (`hostcols_tier`,
  sprint 035): unchanged for `HOSTCOL_HOLD_S` (30 s), then red with
  `NO DATA <age>` in place, its metrics dimmed at their last values; after
  `HOSTCOL_DROP_S` (5 min) an intermittent host takes no slot, and an
  always-on one stays red. A host with no `last_seen_ts` this run (an asleep
  laptop after a panel restart) has nothing to hold on and gets no column.
- Each column/tile is one style-less `lv_obj` drawn in `LV_EVENT_DRAW_MAIN`
  from a pre-formatted model; 30 are created once, and an update only moves,
  resizes, hides, and invalidates the slots whose content changed.

## Weather Filler (sprint 034)

`src/widgets/weather_card.c`, the "C · Range" mock-up Ken picked on
2026-11-12 (`sprints/planning/weather-mockups.html`, function `vC`). A filler:
nothing depends on it.

- **Space** (`wx_columns`, `wx_left`): n = min(4, 10 − the columns the hosts
  use (`hostcols_used`)), width 375·n − 14, the host columns' height. It
  starts right after the last host column; with six hosts or fewer the
  4-column card is centred in what is left (+ ½ · 375 · (6 − hosts)). n = 0,
  or no record ever seen: not drawn.
- **Layout.** Left 361 px: the condition icon, the temperature (96 px bold
  digits), the condition, "feels · humidity", and at n = 1 "today hi lo". At
  n ≥ 2, a bar per day from low to high on one shared temperature scale (3, 6
  or 8 days at 2, 3 or 4 columns, never more than the record has), an opaque
  LVGL peach-to-blue gradient; a day with only one of hi/lo is a dot. Along the
  bottom, the local day midnight to midnight: twilight dim, daylight yellow, a
  marker for now, sunrise/sunset above, first/last light (or the daylight
  length at n = 1) below; "no sunrise today" when the sun events are null.
- **Trust and freshness** (`wxstate.c`). Stale after 1 h by `ts`: everything
  grey and the age in red bold. A record the contract rejects — an unknown
  `cond`, a millisecond stamp, a percent out of range, a required field absent
  rather than null — is logged once and the last good one kept.
- **Icons** are drawn, not images: circles, round-capped lines and four
  triangles for the bolt, all opaque. They recolour for the stale state for
  free and are repainted only when a record arrives.
- **Repaint cost.** One callback-drawn object, as the host columns. A new
  record, width or stale flip repaints the card; otherwise only the age line
  (once a minute) or a strip around the day bar's marker (when its minute
  moves) is invalidated.

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


