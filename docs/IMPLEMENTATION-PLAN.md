# KPI Dashboard — Implementation Plan

## Phase 0: Environment & Repo (DONE)

- [x] Install build tools: `gcc`, `cmake`, `make`, `pkg-config`
- [x] Install LVGL dependencies: `libdrm-dev`, `libinput-dev`, `libxkbcommon-dev`,
      `libfreetype-dev`, `libpng-dev`, `libjpeg-dev`, `libcjson-dev`
- [x] Install `git`, configure user, init repo, create `working` branch
- [x] Create `.gitignore`
- [x] Create planning docs

## Phase 1: LVGL Skeleton

**Goal**: LVGL renders a static "Hello World" fullscreen on the Pi display via
DRM/KMS.

### Steps

1. **Add LVGL as a git submodule**
   ```bash
   git submodule add https://github.com/lvgl/lvgl.git lib/lvgl
   cd lib/lvgl && git checkout release/v9.2 && cd ../..
   ```

2. **Create `lv_conf.h`** in project root
   - Enable DRM driver (`LV_USE_LINUX_DRM`)
   - Set color depth 32 (XRGB8888 matches the Pi's framebuffer)
   - Enable label, LED, and container widgets
   - Set `LV_FONT_MONTSERRAT_20` and `_28` (readable at 4K)

3. **Create `CMakeLists.txt`**
   - Minimum CMake 3.16
   - Add LVGL subdirectory
   - Link `libdrm`, `libinput`, `pthread`
   - Set C11 standard

4. **Create `src/main.c`** with minimal code:
   - Initialize LVGL (`lv_init()`)
   - Create DRM display (`lv_linux_drm_create()`)
   - Create a test label
   - Run `lv_timer_handler()` loop

5. **Build and verify**
   ```bash
   mkdir build && cd build && cmake .. && make -j4
   sudo ./kpidash   # needs DRM access
   ```

### Acceptance
- Fullscreen display with a label visible on the HDMI monitor.

## Phase 2: Client Registry & UDP Listener

**Goal**: receive UDP JSON messages and maintain a client registry.

### Steps

1. **Create `src/protocol.h`** — message type constants, buffer sizes
2. **Create `src/registry.h` / `registry.c`**
   - `client_info_t` struct (see ARCHITECTURE.md)
   - `registry_init()`, `registry_find_or_create(hostname)`,
     `registry_lock()`, `registry_unlock()`
   - Mutex-protected
3. **Create `src/net.h` / `net.c`**
   - `net_start(registry, port)` — spawns UDP listener thread
   - Thread: `recvfrom()` → `cJSON_Parse()` → update registry
   - Handle `"health"` and `"task"` message types
4. **Wire into `main.c`** — start UDP thread before LVGL loop

### Acceptance
- Send test messages via `nc -u` and observe (via printf/log) that the
  registry updates correctly.

## Phase 3: Dashboard UI

**Goal**: dynamic LVGL widgets that reflect the client registry.

### Steps

1. **Create `src/ui.h` / `ui.c`**
   - `ui_init(lv_obj_t *screen)` — create title bar, clock, scrollable list
   - `ui_create_client_card(client_info_t *)` — LED + labels inside a
     container
   - `ui_update_clients(client_info_t *clients, int count)` — called by LVGL
     timer
2. **LVGL timer callback** (every 1 second)
   - Lock registry
   - For each active client:
     - Create card if `container == NULL`
     - Update LED color based on `now - last_health > HEALTH_TIMEOUT`
     - Update task label and elapsed time
   - Unlock registry
3. **Style the cards** — dark background, large fonts for 4K readability
4. **Update clock** in title bar

### Acceptance
- Send health + task messages from a test client; see the card appear on
  screen with correct status, task, and ticking elapsed time.

## Phase 4: Polish & Testing

**Goal**: handle edge cases, test with multiple clients.

### Steps

1. **Multiple clients** — send messages from 3+ different hostnames, verify
   layout scrolls properly.
2. **Health timeout** — stop sending from one client, verify LED turns red
   after timeout.
3. **Task clear** — send an empty task, verify "(no task)" display.
4. **Graceful shutdown** — handle SIGINT: close socket, clean up LVGL.
5. **Buffer overflow** — test oversized hostname/task, verify truncation.
6. **Document build & run** in a `README.md`.

## Phase 5: Client Hand-off

**Goal**: provide everything needed for an agent on the client machine to build
a client daemon.

### Deliverables
- [docs/CLIENT-PROTOCOL.md](CLIENT-PROTOCOL.md) — protocol specification
  (created during planning)
- Tested `nc` commands that prove the protocol works
- Example Python client snippet (included in protocol doc)
- Suggested CLI interface for the client daemon

---

## Implementation Order Summary

| Phase | Deliverable | Depends On |
|-------|-------------|------------|
| 0 | Repo, deps, planning docs | — |
| 1 | LVGL hello-world on display | Phase 0 |
| 2 | UDP listener + client registry | Phase 1 |
| 3 | Dynamic dashboard UI | Phase 2 |
| 4 | Polish, edge cases, README | Phase 3 |
| 5 | Client hand-off docs | Phase 4 |

## Build Commands (Reference)

```bash
# First time
git submodule update --init --recursive
mkdir -p build && cd build
cmake ..
make -j4

# Run (needs DRM access)
sudo ./build/kpidash

# Test messages
echo '{"type":"health","host":"testbox","uptime":1234}' | nc -u -w0 localhost 5555
echo '{"type":"task","host":"testbox","task":"coding","started":'"$(date +%s)"'}' | nc -u -w0 localhost 5555
```
