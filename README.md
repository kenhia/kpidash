# kpidash

A lightweight KPI dashboard for Raspberry Pi 5, built with [LVGL](https://lvgl.io/).
Displays real-time health status and current tasks for multiple client machines.

## Quick Start

```bash
# Clone (with submodules)
git clone --recurse-submodules <repo-url>
cd kpidash

# Build
mkdir -p build && cd build
cmake ..
make -j4

# Run (needs DRM access)
sudo ./kpidash
```

## Configuration

| Environment Variable | Default | Description |
|---------------------|---------|-------------|
| `KPIDASH_PORT` | `5555` | UDP listen port |

## Testing

Send test messages from any machine on the network:

```bash
PI_IP=192.168.1.100  # replace with your Pi's IP

# Health ping
echo '{"type":"health","host":"testbox","uptime":1234}' | nc -u -w0 $PI_IP 5555

# Set a task
echo '{"type":"task","host":"testbox","task":"coding","started":'"$(date +%s)"'}' | nc -u -w0 $PI_IP 5555

# Clear a task
echo '{"type":"task","host":"testbox","task":"","started":0}' | nc -u -w0 $PI_IP 5555
```

### fish shell

```fish
set PI_IP 192.168.1.100
echo '{"type":"health","host":"testbox","uptime":1234}' | nc -u -w0 $PI_IP 5555
echo '{"type":"task","host":"testbox","task":"coding","started":'(date +%s)'}' | nc -u -w0 $PI_IP 5555
```

## Client Protocol

See [docs/CLIENT-PROTOCOL.md](docs/CLIENT-PROTOCOL.md) for the full protocol specification.

## Architecture

See [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) for design details.

## Project Structure

```
kpidash/
├── CMakeLists.txt         # Build configuration
├── lv_conf.h              # LVGL configuration
├── src/
│   ├── main.c             # Entry point, LVGL init, main loop
│   ├── ui.c / ui.h        # Dashboard UI (widgets, styles, layout)
│   ├── net.c / net.h      # UDP listener thread
│   ├── registry.c / .h    # Thread-safe client registry
│   └── protocol.h         # Constants (port, buffer sizes, timeouts)
├── lib/
│   └── lvgl/              # LVGL v9.2.2 (git submodule)
└── docs/
    ├── ARCHITECTURE.md
    ├── CLIENT-PROTOCOL.md
    └── IMPLEMENTATION-PLAN.md
```

## Requirements

- Raspberry Pi 5 (or any Linux system with DRM/KMS)
- Debian 13 (Trixie) or similar
- Packages: `build-essential cmake pkg-config libdrm-dev libinput-dev libxkbcommon-dev libfreetype-dev libpng-dev libjpeg-dev libcjson-dev`
