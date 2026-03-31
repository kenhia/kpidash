# KPI Dashboard — Development Handoff & Cross-Compilation Guide

> **Target audience**: Claude (or developer) on an Ubuntu x86_64 workstation,
> continuing development of kpidash and cross-compiling for Raspberry Pi 5.

---

## 1. Project Overview

**kpidash** is a fullscreen dashboard for Raspberry Pi 5 built with LVGL 9.2.2.
It uses DRM/KMS for direct rendering (no X11/Wayland), receives UDP JSON
messages from client machines, and displays per-client health status and tasks.

### Current state (as of 30 March 2026)

Working POC with:
- Fullscreen LVGL UI on Pi 5 via DRM/KMS (`/dev/dri/card1`)
- UDP listener on port 5555 (health pings, task updates, task_done)
- Dynamic client cards with LED health indicator, task, elapsed time
- PNG image loading from filesystem (libpng + POSIX FS driver)
- Dark theme optimized for 3840×2160 display

### Git history

```
30b0115 Add image widgets for NVMe, HDD, SSD
1e92982 Fix task_done freeze: replace U+2713 checkmark with ASCII
7066505 Add task_done message type
2a45c83 Accept string-encoded numbers in JSON fields, add fish shell examples
fb52723 Fix DRM device: use card1 (vc4 GPU with HDMI) on Pi 5
22a90d7 Implement LVGL dashboard: DRM display, UDP listener, client registry, dynamic UI
01186ec Initial planning: architecture, protocol spec, and implementation plan
```

---

## 2. Target Hardware

| Item | Detail |
|------|--------|
| Board | Raspberry Pi 5 |
| Architecture | aarch64 (ARM64) |
| OS | Debian 13 (Trixie) |
| Kernel | 6.6.20+rpt-rpi-2712 |
| Display | HDMI-A-1 on `/dev/dri/card1`, 3840×2160 @ 30 Hz |
| GPU driver | vc4-kms-v3d (DRM/KMS) |
| DRI note | `card0` is RP1 (no dumb buffer support); `card1` is the vc4 GPU. Configurable via `KPIDASH_DRM_DEV` env var. |

---

## 3. Project Structure

```
kpidash/
├── CMakeLists.txt              # Build config (C11, links LVGL + system libs)
├── lv_conf.h                   # LVGL configuration (see Section 5)
├── src/
│   ├── main.c                  # Entry: DRM display init, UDP thread, LVGL loop
│   ├── ui.c / ui.h             # Dashboard UI: title bar, image row, client cards
│   ├── net.c / net.h           # UDP listener thread (cJSON parsing)
│   ├── registry.c / registry.h # Thread-safe client array (mutex-protected)
│   └── protocol.h              # Constants: port, buffer sizes, timeouts
├── resources/                  # PNG images (nvme.png, hdd.png, ssd.png)
├── lib/
│   └── lvgl/                   # LVGL v9.2.2 (git submodule, tag v9.2.2)
└── docs/
    ├── ARCHITECTURE.md
    ├── CLIENT-PROTOCOL.md
    └── IMPLEMENTATION-PLAN.md
```

---

## 4. Dependencies (Target)

These packages are installed on the Pi and must be available as arm64 libraries
when cross-compiling:

| Library | Debian package (arm64) | pkg-config name | Used by |
|---------|----------------------|-----------------|---------|
| libdrm | `libdrm-dev` | `libdrm` | LVGL DRM driver |
| libinput | `libinput-dev` | — | LVGL (optional, for touch) |
| libxkbcommon | `libxkbcommon-dev` | — | LVGL (optional, for keyboard) |
| freetype | `libfreetype-dev` | — | LVGL font rendering |
| libpng | `libpng-dev` | `libpng` | LVGL image decoder |
| libjpeg | `libjpeg-dev` | — | LVGL (optional) |
| cJSON | `libcjson-dev` | `libcjson` | UDP message parsing |

---

## 5. LVGL Configuration (`lv_conf.h`)

This is the authoritative config. Key settings and **why**:

```c
/* 32-bit color matches the Pi's DRM XRGB8888 framebuffer */
#define LV_COLOR_DEPTH 32

/* Standard C library — not LVGL's built-in allocator */
#define LV_USE_STDLIB_MALLOC    LV_STDLIB_CLIB
#define LV_USE_STDLIB_STRING    LV_STDLIB_CLIB
#define LV_USE_STDLIB_SPRINTF   LV_STDLIB_CLIB

/* ~30 fps refresh, 160 DPI for 4K at comfortable viewing distance */
#define LV_DEF_REFR_PERIOD  33
#define LV_DPI_DEF 160

/* Pthreads — required for our mutex-based threading model */
#define LV_USE_OS   LV_OS_PTHREAD

/* Logging at WARN level, printf-based */
#define LV_USE_LOG 1
#define LV_LOG_LEVEL LV_LOG_LEVEL_WARN
#define LV_LOG_PRINTF 1

/* Fonts: Montserrat at sizes used by UI. Default is 20. */
#define LV_FONT_MONTSERRAT_14  1
#define LV_FONT_MONTSERRAT_20  1
#define LV_FONT_MONTSERRAT_24  1
#define LV_FONT_MONTSERRAT_28  1
#define LV_FONT_MONTSERRAT_36  1
#define LV_FONT_DEFAULT &lv_font_montserrat_20

/* LED widget (used for health status indicator) */
#define LV_USE_LED 1

/* DRM/KMS display driver — no X11/Wayland required */
#define LV_USE_LINUX_DRM 1

/* POSIX filesystem — for loading PNG images at runtime */
#define LV_USE_FS_POSIX 1
#define LV_FS_POSIX_LETTER 'A'         /* paths: "A:/absolute/path.png" */
#define LV_FS_POSIX_PATH ""            /* no prefix — use full absolute paths */
#define LV_FS_POSIX_CACHE_SIZE 0

/* PNG decoder via system libpng */
#define LV_USE_LIBPNG 1
```

### Gotchas discovered during development

1. **Unicode glyphs**: Montserrat built-in fonts do NOT include symbols like
   ✓ (U+2713). Using them crashes/freezes the render loop. Stick to ASCII or
   add a custom symbol font.

2. **DRM device path**: On Pi 5, `/dev/dri/card0` is the RP1 display
   controller (no dumb buffer support). The vc4 GPU with HDMI is `card1`.
   This is configurable via `KPIDASH_DRM_DEV` env var (default: `/dev/dri/card1`).

3. **Image paths**: LVGL's POSIX FS driver uses a drive letter prefix. Files
   are referenced as `"A:/absolute/path/to/file.png"`.

4. **Threading**: LVGL is NOT thread-safe. The UDP listener thread writes to
   the client registry (mutex-protected), and the LVGL timer callback reads
   from it. Never call LVGL functions from the UDP thread.

---

## 6. Cross-Compilation from Ubuntu x86_64

### 6.1 Install the cross toolchain

```bash
sudo apt-get update
sudo apt-get install -y \
    gcc-aarch64-linux-gnu g++-aarch64-linux-gnu \
    cmake pkg-config
```

### 6.2 Set up an arm64 sysroot

The cleanest approach is to rsync the necessary libraries from the Pi:

```bash
SYSROOT=$HOME/pi5-sysroot
mkdir -p $SYSROOT

# Sync lib, usr/lib, and usr/include from the Pi
rsync -avz --rsync-path="sudo rsync" \
    ken@PI_IP:/lib/aarch64-linux-gnu $SYSROOT/lib/
rsync -avz --rsync-path="sudo rsync" \
    ken@PI_IP:/usr/lib/aarch64-linux-gnu $SYSROOT/usr/lib/
rsync -avz ken@PI_IP:/usr/include $SYSROOT/usr/

# Fix absolute symlinks to be relative within sysroot
cd $SYSROOT
find . -type l | while read link; do
    target=$(readlink "$link")
    if [[ "$target" == /* ]]; then
        # Make absolute symlink relative to sysroot
        ln -sf "$SYSROOT$target" "$link" 2>/dev/null || true
    fi
done
```

**Alternative**: install arm64 packages via multiarch on Ubuntu:
```bash
sudo dpkg --add-architecture arm64
sudo apt-get update
sudo apt-get install -y \
    libdrm-dev:arm64 libpng-dev:arm64 libcjson-dev:arm64 \
    libfreetype-dev:arm64 libinput-dev:arm64 libxkbcommon-dev:arm64
```
(Multiarch can cause dependency conflicts — the sysroot approach is more reliable.)

### 6.3 CMake toolchain file

Create `cmake/aarch64-toolchain.cmake`:

```cmake
set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR aarch64)

set(CMAKE_C_COMPILER aarch64-linux-gnu-gcc)
set(CMAKE_CXX_COMPILER aarch64-linux-gnu-g++)

# Point to the sysroot with arm64 libraries
set(CMAKE_SYSROOT $ENV{HOME}/pi5-sysroot)
set(CMAKE_FIND_ROOT_PATH ${CMAKE_SYSROOT})

# Search headers/libs only in sysroot; programs on host
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

# pkg-config must search the sysroot
set(ENV{PKG_CONFIG_PATH} "${CMAKE_SYSROOT}/usr/lib/aarch64-linux-gnu/pkgconfig")
set(ENV{PKG_CONFIG_SYSROOT_DIR} "${CMAKE_SYSROOT}")
```

### 6.4 Build for Pi 5

```bash
cd kpidash
git submodule update --init --recursive

mkdir -p build-pi5 && cd build-pi5
cmake -DCMAKE_TOOLCHAIN_FILE=../cmake/aarch64-toolchain.cmake ..
make -j$(nproc)

# Verify it's an aarch64 binary
file kpidash
# → ELF 64-bit LSB pie executable, ARM aarch64, ...
```

### 6.5 Deploy to Pi

```bash
scp build-pi5/kpidash ken@PI_IP:~/src/kpidash/build/kpidash
# Also sync any new resources:
rsync -avz resources/ ken@PI_IP:~/src/kpidash/resources/
```

### 6.6 Native build (for reference)

On the Pi itself, the native build is:
```bash
mkdir -p build && cd build
cmake ..
make -j4
sudo ./kpidash
```

---

## 7. Runtime

```bash
# Run (needs DRM access for /dev/dri/card1)
sudo ./kpidash

# Or with custom port / device:
sudo KPIDASH_PORT=6000 KPIDASH_DRM_DEV=/dev/dri/card1 ./kpidash

# Test from any machine:
echo '{"type":"health","host":"mybox","uptime":1234}' | nc -u -w0 PI_IP 5555
echo '{"type":"task","host":"mybox","task":"coding","started":'"$(date +%s)"'}' | nc -u -w0 PI_IP 5555
echo '{"type":"task_done","host":"mybox"}' | nc -u -w0 PI_IP 5555
```

---

## 8. LVGL API Patterns Used

### Display initialization (DRM)
```c
lv_init();
lv_display_t *disp = lv_linux_drm_create();
lv_linux_drm_set_file(disp, "/dev/dri/card1", -1);  // -1 = auto connector
```

### Main loop
```c
while (running) {
    uint32_t sleep_ms = lv_timer_handler();
    if (sleep_ms > 100) sleep_ms = 100;
    usleep(sleep_ms * 1000);
}
```

### Widget patterns used
- **Flex layout**: `lv_obj_set_flex_flow()`, `lv_obj_set_flex_align()`
- **LED**: `lv_led_create()`, `lv_led_set_color()`, `lv_led_on()`
- **Labels**: `lv_label_create()`, `lv_label_set_text()`
- **Images**: `lv_image_create()`, `lv_image_set_src("A:/path.png")`
- **Styles**: `lv_style_init()`, `lv_style_set_*()`, `lv_obj_add_style()`
- **Timer**: `lv_timer_create(callback, period_ms, NULL)`

### Style application
```c
static lv_style_t style_card;
lv_style_init(&style_card);
lv_style_set_bg_color(&style_card, lv_color_hex(0x16213e));
lv_style_set_bg_opa(&style_card, LV_OPA_COVER);
lv_style_set_radius(&style_card, 12);
// Apply to an object:
lv_obj_add_style(obj, &style_card, 0);
```

---

## 9. Protocol Summary

Three message types over UDP port 5555 (JSON):

| Type | Purpose | Key fields |
|------|---------|------------|
| `health` | Periodic alive ping | `host`, `uptime` (optional) |
| `task` | Set/replace current task | `host`, `task`, `started` |
| `task_done` | Complete current task | `host` |

Numeric fields (`uptime`, `started`) accept both JSON numbers and numeric strings.

Full spec: `docs/CLIENT-PROTOCOL.md`

---

## 10. Known Limitations / Future Work

- Image paths are hardcoded to absolute paths in `ui.c` — should be relative
  to a configurable resource directory.
- No systemd service file yet.
- No client deregistration — slots are never freed.
- The image widgets (NVMe/HDD/SSD) are placeholders for POC validation; will
  need real dashboard content.
- Consider adding `LV_USE_FONT_PLACEHOLDER 1` to gracefully handle missing
  glyphs instead of display corruption.
