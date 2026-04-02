# kpidash

A fullscreen KPI dashboard for Raspberry Pi 5, built with [LVGL](https://lvgl.io/) and Redis.
Displays real-time health, telemetry, activities, repo status, and fortune messages for multiple
client machines — Linux and Windows.

## Dashboard layout

```
┌─────────────────────────────────────────────┐
│  Client cards (health LED, CPU/RAM/disk)    │
├────────────────────┬────────────────────────┤
│  Activities        │  Repo Status           │
├────────────────────┴────────────────────────┤
│  Fortune                                    │
└─────────────────────────────────────────────┘
```

## Quick start

### 1. Clone

```bash
git clone --recurse-submodules <repo-url>
cd kpidash
```

### 2. Cross-compile for Pi 5

```bash
# Install cross toolchain (Ubuntu/Debian)
sudo apt-get install gcc-aarch64-linux-gnu g++-aarch64-linux-gnu cmake pkg-config

# Sync Pi sysroot (replace rpi53 with your Pi hostname/IP)
SYSROOT=$HOME/pi5-sysroot && mkdir -p $SYSROOT
rsync -avz --rsync-path="sudo rsync" ken@rpi53:/lib/aarch64-linux-gnu $SYSROOT/lib/
rsync -avz --rsync-path="sudo rsync" ken@rpi53:/usr/lib/aarch64-linux-gnu $SYSROOT/usr/lib/
rsync -avz ken@rpi53:/usr/include $SYSROOT/usr/

# Build
cmake -B build-pi -DCMAKE_TOOLCHAIN_FILE=cmake/aarch64-toolchain.cmake
cmake --build build-pi --target kpidash -j$(nproc)

# Deploy
rsync -az build-pi/kpidash ken@rpi53:~/kpidash
```

### 3. Run on Pi 5

```bash
# DRM requires root (or video group membership); -E preserves REDISCLI_AUTH
export REDISCLI_AUTH=yourpassword
sudo -E ./kpidash
```

### 4. Start a client daemon (Linux or Windows)

```bash
cd clients/kpidash-client
uv sync                       # Python 3.13 venv, see .python-version
uv run kpidash-client daemon start
```

See [clients/kpidash-client/README.md](clients/kpidash-client/README.md) for config file setup.

---

## Configuration (dashboard)

| Environment Variable | Default | Description |
|---------------------|---------|-------------|
| `KPIDASH_REDIS_HOST` | `127.0.0.1` | Redis host |
| `KPIDASH_REDIS_PORT` | `6379` | Redis port |
| `REDISCLI_AUTH` | (none) | Redis password — **must** be in env when using `sudo -E` |
| `KPIDASH_DRM_DEV` | `/dev/dri/card1` | DRM device (card1 = vc4 GPU on Pi 5) |
| `KPIDASH_MAX_CLIENTS` | `16` | Max tracked clients |
| `KPIDASH_ACTIVITY_MAX` | `10` | Max activities shown |
| `KPIDASH_LOG_FILE` | `/var/log/kpidash/kpidash.log` | Log path written to Redis |
| `KPIDASH_PRIORITY_CLIENTS` | (none) | Comma-separated hostnames; never evicted from registry |

---

## Native unit tests (no hardware required)

```bash
cmake -B build-tests -DTESTS_ONLY=ON
cmake --build build-tests
ctest --test-dir build-tests -V
# 51 tests: config env-var parsing + Redis JSON parsing
```

---

## Python clients

| Package | Purpose | Docs |
|---------|---------|------|
| `clients/kpidash-client` | Telemetry daemon + CLI (Linux, Windows) | [README](clients/kpidash-client/README.md) |
| `clients/kpidash-mcp` | MCP server for AI agent activity reporting | [README](clients/kpidash-mcp/README.md) |

Both packages are managed with [uv](https://docs.astral.sh/uv/) and pinned to Python 3.13.

---

## Project structure

```
kpidash/
├── CMakeLists.txt              # CMake 3.22+, hiredis, cJSON, libdrm, LVGL
├── lv_conf.h                   # LVGL configuration
├── cmake/
│   └── aarch64-toolchain.cmake # Cross-compile toolchain for Pi 5
├── src/
│   ├── main.c                  # Entry: config, LVGL init, DRM, poll timers
│   ├── config.{h,c}            # Environment variable parsing
│   ├── registry.{h,c}          # In-memory client state (mutex-protected)
│   ├── redis.{h,c}             # hiredis poll cycle + cJSON parsing
│   ├── status.{h,c}            # Status message FIFO queue
│   ├── fortune.{h,c}           # fortune popen + pushed-fortune override
│   ├── ui.{h,c}                # Screen layout + Redis error overlay
│   ├── protocol.h              # Redis key macros + compile-time constants
│   └── widgets/
│       ├── client_card.{h,c}   # Per-client health/telemetry card
│       ├── activities.{h,c}    # Activity list with live elapsed timers
│       ├── repo_status.{h,c}   # Repo dirty/branch status list
│       ├── fortune.{h,c}       # Fortune text label
│       └── status_bar.{h,c}    # Bottom status bar
├── tests/
│   ├── test_config.c           # ctest: config env-var parsing
│   └── test_redis_json.c       # ctest: Redis JSON parsing helpers
├── clients/
│   ├── kpidash-client/         # Python 3.13 daemon + CLI
│   └── kpidash-mcp/            # Python 3.13 MCP server
├── scripts/
│   └── load_test.py            # 8-client concurrent write stress test
├── lib/
│   └── lvgl/                   # LVGL v9.2.2 (git submodule)
├── specs/
│   └── 001-mvp-dashboard/      # Spec, plan, tasks, contracts, data model
└── docs/
    ├── ARCHITECTURE.md         # System design and component diagram
    ├── CLIENT-PROTOCOL.md      # Redis key/value schema reference
    └── HANDOFF-CROSSCOMPILE.md # Cross-compilation guide
```

## Requirements

- **Pi 5**: Debian 13 (Trixie), Redis 7.x, `fortune` package
- **Pi packages**: `libdrm-dev libpng-dev libcjson-dev libhiredis-dev libfreetype-dev libinput-dev libxkbcommon-dev redis-server fortune`
- **Cross-compile host**: Ubuntu 22.04+ x86_64, GCC aarch64 toolchain, CMake 3.22+
- **Python clients**: Python 3.13 via [uv](https://docs.astral.sh/uv/)
