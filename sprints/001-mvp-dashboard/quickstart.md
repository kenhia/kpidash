# Quickstart: kpidash MVP Development

**Branch**: `001-mvp-dashboard`
**Audience**: Developer setting up for the first time

---

## Prerequisites

### Raspberry Pi 5 (display host — aarch64)

- Debian 13 Trixie installed
- Redis server running: `sudo apt install redis-server`; set a password in
  `/etc/redis/redis.conf` (`requirepass yourpassword`)
- `fortune` installed: `sudo apt install fortune`
- Packages for the dashboard build already installed:
  ```
  sudo apt install libdrm-dev libinput-dev libxkbcommon-dev \
    libfreetype-dev libpng-dev libjpeg-dev libcjson-dev \
    libhiredis-dev
  ```
- Environment: `export REDISCLI_AUTH=yourpassword`

### x86_64 Build Machine (Ubuntu/Debian)

Cross-compilation toolchain for aarch64:
```bash
sudo apt install gcc-aarch64-linux-gnu binutils-aarch64-linux-gnu
sudo apt install cmake ninja-build pkg-config git

# Sysroot libraries for arm64 cross-compilation
sudo dpkg --add-architecture arm64
sudo apt update
sudo apt install libdrm-dev:arm64 libpng-dev:arm64 libcjson-dev:arm64 \
  libhiredis-dev:arm64 libinput-dev:arm64 libxkbcommon-dev:arm64 \
  libfreetype-dev:arm64 libjpeg-dev:arm64
```

### Python Client Machines (Linux or Windows)

- Python 3.11+
- `REDISCLI_AUTH` environment variable set to the Redis password
- Access to the Redis server on the Pi 5 (same LAN, port 6379)

---

## Build the Dashboard (cross-compile for Pi 5)

```bash
# Clone with submodules
git clone --recurse-submodules <repo-url>
cd kpidash

# Cross-compile for aarch64
cmake -B build-pi \
  -DCMAKE_TOOLCHAIN_FILE=cmake/aarch64-toolchain.cmake \
  -DCMAKE_BUILD_TYPE=Release
cmake --build build-pi --parallel

# Transfer to Pi
scp build-pi/kpidash pi@<PI_IP>:~/kpidash
```

The binary `kpidash` has no Python dependency and links against system
libraries on the Pi.

---

## Run the Dashboard on Pi 5

```bash
# On the Pi
export REDISCLI_AUTH=yourpassword
export KPIDASH_DRM_DEV=/dev/dri/card1      # Pi 5 vc4 GPU (default)
export KPIDASH_PORT=6379                   # Redis port

sudo ./kpidash                             # DRM requires root or video group
```

The dashboard starts fullscreen, connects to Redis on localhost, and shows a
blank layout until clients connect.

---

## Install the Linux Client

```bash
# On any Linux machine
cd clients/kpidash-client
pip install -e .          # development install

# Or install from a release:
# pip install kpidash-client

# Configure
mkdir -p ~/.config/kpidash-client
cat > ~/.config/kpidash-client/config.toml << 'EOF'
[redis]
host = "192.168.1.50"    # Pi 5 IP
port = 6379

[telemetry]
interval_seconds = 5

[[disks]]
path = "/"
label = "root"
type = "nvme"

[repos]
scan_roots = ["/home/youruser/src"]
scan_depth = 2
EOF

# Set Redis password
export REDISCLI_AUTH=yourpassword

# Start daemon
kpidash-client daemon start
```

The machine's card should appear on the dashboard within 10 seconds.

---

## Client CLI Quick Reference

```bash
# Activity tracking
kpidash-client activity start "writing MVP spec"
kpidash-client activity done

# Push a fortune
kpidash-client fortune push "Stay curious."

# Acknowledge a dashboard status/warning
kpidash-client status ack

# Get dashboard log path (then scp it)
kpidash-client log-path
# → /var/log/kpidash/kpidash.log
# scp pi@<PI_IP>:/var/log/kpidash/kpidash.log ./kpidash.log
```

---

## Install the MCP Server

```bash
cd clients/kpidash-mcp
pip install -e .

# Configure your AI agent (example: Claude Desktop config.json)
{
  "mcpServers": {
    "kpidash": {
      "command": "kpidash-mcp",
      "env": {
        "KPIDASH_REDIS_HOST": "192.168.1.50",
        "REDISCLI_AUTH": "yourpassword"
      }
    }
  }
}
```

The MCP server exposes `start_activity` and `end_activity` tools. The AI
agent can report its work to the dashboard automatically.

---

## Build Verification (Pre-commit)

```bash
# Format check
clang-format --dry-run --Werror src/*.c src/*.h

# Static analysis
cppcheck --enable=warning,style,performance --error-exitcode=1 src/

# Native build (for development on Linux x86_64)
cmake -B build && cmake --build build --parallel
ctest --test-dir build -V

# Python clients
cd clients/kpidash-client && ruff format --check . && ruff check . && pytest -q
cd clients/kpidash-mcp && ruff format --check . && ruff check . && pytest -q
```

---

## Common Issues

| Symptom | Likely cause | Fix |
|---------|-------------|-----|
| Dashboard blank, no clients shown | Redis not reachable | Check `REDISCLI_AUTH`, firewall, Redis binding (`bind 0.0.0.0` in redis.conf) |
| Client card stays red immediately | Health TTL set wrong or client not sending pings | Ensure `kpidash-client daemon start` is running |
| DRM device error on Pi | Wrong DRI device | Set `KPIDASH_DRM_DEV=/dev/dri/card1` |
| `fortune` widget blank | `fortune` not installed on Pi | `sudo apt install fortune` |
| GPU fields missing on client card | NVML unavailable | Install NVIDIA drivers; check `nvidia-smi` works |
