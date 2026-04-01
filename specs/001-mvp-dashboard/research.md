# Research: kpidash MVP — Technology Decisions

**Branch**: `001-mvp-dashboard`
**Phase**: 0 (Pre-Design)
**Date**: 2026-03-31

---

## Decision 1 — Client Application Language

**Chosen**: Python (3.11+)

**Rationale**:
- `psutil` is the most mature cross-platform system metrics library (CPU, RAM,
  disk — Linux and Windows), 15+ years of production use.
- `pynvml` provides official Python bindings to NVIDIA's NVML C library,
  supporting GPU name, VRAM, and compute usage on both Linux and Windows.
- `redis-py` is the reference Redis client, with synchronous and async modes.
- `GitPython` covers branch/dirty detection with a clean high-level API (stable,
  maintenance mode is not a concern for our read-only scanning use case).
- `click` is the de facto Python CLI framework — minimal boilerplate.
- The MCP Python SDK (`mcp`) is the reference implementation by Anthropic
  (22k+ GitHub stars vs 12k for the TypeScript SDK), with the most
  documentation and community examples. MCP servers are also most commonly
  distributed and configured as Python packages.
- One Python package can target both Linux and Windows with platform-specific
  branches for telemetry only — avoids maintaining two separate codebases.
- Distribution on Windows: PyInstaller produces a single `.exe` for MVP;
  no Windows Service registration required.
- Development speed advantage is significant given the breadth of the MVP spec.

**Alternatives considered**:

| Option | Reason rejected |
|--------|----------------|
| Rust | Longer feedback loop; MCP Rust crate less mature than Python SDK; distribution advantage outweighed by dev complexity for MVP timeline |
| Go | Solid tooling but MCP ecosystem minimal; no compelling advantage over Python here |
| C# / .NET | Windows-first legacy; cross-platform support less proven for embedded tooling |
| TypeScript / Node.js | Good MCP SDK; but runtime footprint and daemon packaging heavier than Python |

**Disk type detection caveat — Windows**: `psutil` returns filesystem type
(NTFS/FAT32) but not storage medium type (NVMe/SSD/HDD). Resolution: disk
configuration (see Decision 5 below) includes an explicit `type` field, so
users label their disks. Actual capacity/usage is still measured via `psutil`.
On Linux, medium type is auto-detected from `/sys/block/{dev}/queue/rotational`
and device name prefix (`nvme` path → NVMe).

---

## Decision 2 — C Redis Client (Dashboard)

**Chosen**: hiredis v1.2.0 (Debian Trixie system package: `libhiredis-dev`)

**Rationale**:
- hiredis is the official, maintained C client from the Redis maintainers; RESP2
  and RESP3 protocol support, binary-safe reply parsing.
- Available as pre-built arm64 binary in Debian 13 Trixie (`libhiredis-dev
  1.2.0-6`), making cross-compilation straightforward — no need to build from
  source.
- CMake `find_package(hiredis)` integration with target `hiredis::hiredis`.
- If for any reason the system package is unavailable,
  `FetchContent_Declare(hiredis GIT_TAG v1.2.0)` provides a hermetic fallback.

**Alternatives considered**:

| Option | Reason rejected |
|--------|----------------|
| libvalkey | C library not yet available (only C# client); Redis-fork C library is still hiredis-derived |
| credis / libredis | Unmaintained (last activity ~2007) |
| Raw RESP2 sockets | Reinvents hiredis's proven parser; no benefit |

---

## Decision 3 — Dashboard Redis Integration Pattern

**Chosen**: Single-threaded synchronous polling at 1-second interval

**Rationale**: The LVGL rendering loop calls `lv_timer_handler()` in a tight
loop with a ~33ms delay. A secondary Redis I/O operation that could block
would stall the render loop. Options evaluated:

| Pattern | Verdict |
|---------|---------|
| **Synchronous poll, non-blocking 1ms timeout** ✅ | Simple; single thread; 1s latency fully acceptable for a status dashboard; hiredis can use non-blocking I/O with `REDIS_BLOCK` flag set to 1ms; no sync primitives needed |
| Async hiredis + libev | Two event loops competing (LVGL + libev); synchronization complexity; latency improvement unnecessary for 5s telemetry cycle |
| Dedicated Redis thread + mutex | Works but adds complexity (mutex everywhere in C, LVGL not thread-safe on reads) |
| Redis pub/sub | Better latency but subscription management in C requires reconnect logic; adds ~200 lines of state machine code for no measurable user benefit at 5s telemetry intervals |

**Implementation**: A `redis_poll()` function called from the LVGL timer
callback every 1 second. It issues a pipeline of read commands (HGETALL +
SMEMBERS), reads replies with 1ms timeout, and updates shared in-memory
structs. The LVGL render loop reads only those structs — never touches Redis
directly.

---

## Decision 4 — Redis Schema Approach

**Chosen**: Per-client hash keys with TTLs; sorted set for activities; simple
string keys for global singleton data.

**Rationale**:

- **Hashes** allow atomic partial-field updates from Python (HSET one metric at
  a time) without lock contention. C reads HGETALL once per poll cycle —
  one round-trip per client.
- **TTLs** on health and telemetry keys provide implicit offline detection at
  no extra code cost: if `kpidash:client:{host}:health` expires, client is
  offline.
- **SMEMBERS** on a client registry set gives the dashboard the current set of
  known hostnames without pattern-scanning.
- **ZADD** sorted set for activities gives ordering by timestamp with O(log N)
  insert and O(N) range read — simple and correct.
- **Simple string keys** for singletons (fortune, log path, status message) are
  the most direct mapping to their semantics.

See `contracts/redis-schema.md` for the complete key/value contract.

---

## Decision 5 — Client Configuration Format

**Chosen**: TOML (Python `tomllib`, stdlib since Python 3.11)

**Rationale**: TOML is human-readable and unambiguous for the config surface
(lists of paths, simple key-value pairs). Python 3.11+ includes `tomllib` in
the standard library — zero extra dependencies. Config file lives at
`~/.config/kpidash-client/config.toml` (XDG-compliant) on Linux, and at
`%APPDATA%\kpidash-client\config.toml` on Windows.

Sample client config:
```toml
[redis]
host = "192.168.1.50"     # Pi 5 IP or hostname
port = 6379               # default Redis port

[telemetry]
interval_seconds = 5

[[disks]]
path = "/"
label = "root"
type = "nvme"

[[disks]]
path = "/mnt/data"
label = "data"
type = "hdd"

[repos]
explicit = [
  "/home/ken/src/kpidash",
  "/home/ken/src/kpidash-client",
]
scan_roots = [
  "/home/ken/src",
]
scan_depth = 2
exclude = [
  "/home/ken/src/vendor",
]
```

`REDISCLI_AUTH` environment variable provides the Redis password (matching the
dashboard's convention). Redis hostname/port in config; password always from
environment.

---

## Decision 6 — Client Package Structure

**Chosen**: Single shared Python package (`clients/kpidash-client/`) for all
platforms, plus a separate package for the MCP server
(`clients/kpidash-mcp/`).

**Rationale**: Linux and Windows telemetry collection differ only in a few
platform-dispatch points (disk type auto-detection, daemon startup). All Redis
communication, CLI commands, activity tracking, and repo scanning are
100% identical. One package with `platform.system()` dispatch is far simpler
than two diverging codebases (Principle V).

The MCP server is a separate package because it has a different dependency
tree (`mcp` SDK), different deployment lifecycle (used inside an AI agent
session rather than as a persistent daemon), and different configuration
semantics. It reuses the same Redis key schema but is independent of the
client daemon.

---

## Decision 7 — C Source Module Structure (Dashboard Refactor)

**Chosen**: Refactor the existing flat `src/` into logical modules within
`src/`. Keep C11, keep CMake, keep LVGL 9.x with DRM/KMS backend.

The POC `net.c`/`net.h` (UDP) is replaced by `redis.c`/`redis.h` (hiredis
polling). The POC `ui.c` is refactored and expanded into widget-per-file
under `src/widgets/`. The POC title bar is removed; the registry is retained
and extended.

Specific retained: `registry.c/h` (client array, mutex), `protocol.h`
(constants updated for Redis context), `main.c` (updated for Redis init).

Specific replaced/removed: `net.c/h` (UDP listener → Redis polling),
`ui.c/h` (flat → widget modules), title bar rendering code.

---

## Unresolved Items

None. All NEEDS CLARIFICATION items are resolved.
