# Quickstart: Exploration Sprint

**Branch**: `002-exploration-sprint`

## Prerequisites

- Sprint 001 (MVP) fully functional: dashboard renders, clients report telemetry via Redis
- Cross-compilation toolchain working: `cmake -B build-pi -DCMAKE_TOOLCHAIN_FILE=cmake/aarch64-toolchain.cmake`
- Pi 5 at `ken@rpi53` accessible via ssh/rsync
- Redis accessible with `REDISCLI_AUTH` set

## Build & Deploy (same as 001)

```bash
# Native tests
cmake -B build-tests -DTESTS_ONLY=ON && cmake --build build-tests && ctest --test-dir build-tests -V

# Cross-compile for Pi
cmake -B build-pi -DCMAKE_TOOLCHAIN_FILE=cmake/aarch64-toolchain.cmake
cmake --build build-pi --parallel
rsync build-pi/kpidash ken@rpi53:~/kpidash

# Run on Pi
ssh ken@rpi53 'sudo -E ./kpidash'
```

## Python Client Update

```bash
cd clients/kpidash-client
uv run kpidash-client daemon stop
# (make code changes for os_name)
uv run kpidash-client daemon start
```

## Development Commands (new in 002)

```bash
# Enable grid overlay (50px spacing)
redis-cli SET kpidash:cmd:grid '{"enabled":true,"size":50}' EX 300

# Disable grid
redis-cli SET kpidash:cmd:grid '{"enabled":false}' EX 10

# Show text size reference
redis-cli SET kpidash:cmd:textsize '{"enabled":true}' EX 300

# Hide text size reference
redis-cli DEL kpidash:cmd:textsize
```

## Iteration Workflow

This sprint is iterative. The typical cycle:

1. Make a C widget change in `src/widgets/`
2. Cross-compile and rsync to Pi
3. Use grid overlay to check sizing/alignment
4. Adjust and repeat

Use `kpidash-client fortune push "rebuild"` to verify the Redis connection is working after changes.

## Key Files to Modify

| File | Change |
|------|--------|
| `src/widgets/client_card.c` | Rewrite: text → arc gauges |
| `src/widgets/dev_grid.c` | New: grid overlay widget |
| `src/widgets/dev_textsize.c` | New: text size reference widget |
| `src/redis.c` | Add command key polling, os_name parsing |
| `src/registry.h` | Add `os_name` field to `client_info_t` |
| `src/protocol.h` | Add command key macros |
| `src/ui.c` | Layout adjustments for new widget sizes |
| `src/widgets/activities.c` | Resize, font changes |
| `src/widgets/repo_status.c` | Dim host prefix, resize |
| `clients/kpidash-client/.../system.py` | Add os_name collection |
| `clients/kpidash-client/.../redis_client.py` | Include os_name in health JSON |
