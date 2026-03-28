# KPI Dashboard — Architecture

## Overview

A lightweight, fullscreen dashboard running on a Raspberry Pi 5 that displays
real-time status for multiple client machines. Clients send UDP messages
containing health pings and current-task updates. The dashboard renders a
per-client widget showing hostname, health status (green/red LED), and current
task with elapsed time.

## Goals

- **POC scope**: get a working dashboard on the Pi 5 display in minimal time.
- **Simple protocol**: UDP + JSON — no HTTP server, no connection management.
- **Extensible later**: the protocol and architecture can grow (REST, TLS,
  authentication) once the POC proves out the concept.

## Hardware / Environment

| Item | Detail |
|------|--------|
| Board | Raspberry Pi 5 |
| OS | Debian 13 (Trixie), kernel 6.6 |
| Display | HDMI-A-1, 3840×2160 @ 30 Hz |
| Graphics | DRM/KMS via `vc4-kms-v3d` overlay |
| DRI device | `/dev/dri/card0` |

## Technology Choices

| Component | Choice | Rationale |
|-----------|--------|-----------|
| Language | C (C11) | LVGL is C-native; avoids binding friction |
| UI toolkit | LVGL 9.x | Lightweight, embedded-friendly, excellent Pi support |
| Display backend | LVGL `lv_linux_drm` driver | Direct DRM/KMS, no X11/Wayland needed |
| Input backend | `libinput` via LVGL driver | Touch/mouse if needed later |
| Transport | UDP (port 5555) | Zero connection overhead, fire-and-forget |
| Serialization | JSON via cJSON | Tiny, single-file C lib; human-readable messages |
| Build system | CMake | LVGL ships CMakeLists; standard for C projects |

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
