# Quickstart — Sprint 006

How to build, test, deploy, and exercise the sprint-006 deliverables.

---

## Prerequisites

- Ubuntu 22.04 (or compatible) for native development; Raspberry Pi 5 sysroot at `~/pi5-sysroot` for cross-compile.
- `cmake ≥ 3.22`, `clang-format`, `cppcheck`, `pkg-config`, `libdrm-dev`, `libpng-dev`, `libcjson-dev`, `libhiredis-dev`, X11 dev headers (for native LVGL backend).
- Python ≥ 3.11 with `uv` (for the client package).
- `npm install -g lv_font_conv` (only when regenerating fonts).

---

## 1. Native build (dashboard + tests)

```bash
cmake -B build
cmake --build build --parallel
ctest --test-dir build -V
```

Run the dashboard against a local Redis (uses your X11 display):

```bash
REDISCLI_AUTH=… ./build/kpidash
```

## 2. Native mockup build (NEW in sprint 006)

The `kpidash-mockup` target opens a small LVGL X11 window populated only with the footer region and stubbed Service Status Cards. Useful for iterating on card width, border thickness, icon size, and font choices without deploying to the Pi.

```bash
# Same build dir as above — the mockup target is added to the native configuration.
cmake --build build --target kpidash-mockup --parallel
./build/kpidash-mockup
```

The mockup target is **excluded** from the Pi cross-compile and from `TESTS_ONLY=ON` builds (per FR-031).

## 3. Cross-compile and deploy to the Pi

```bash
cmake -B build-pi -DCMAKE_TOOLCHAIN_FILE=cmake/aarch64-toolchain.cmake
cmake --build build-pi --parallel
cmake --build build-pi --target deploy   # scp + systemctl restart on rpi53
```

Verify the deployed binary picks up the new layout, footer cards, and per-host graphs by:

```bash
ssh ken@rpi53 'sudo journalctl -u kpidash -f'
```

## 4. Pre-commit checks (per constitution §IV)

```bash
# C
clang-format --dry-run --Werror src/*.c src/*.h src/widgets/*.c src/widgets/*.h
cppcheck --enable=warning,style,performance --error-exitcode=1 src/
cmake --build build --parallel
ctest --test-dir build -V

# Python client
cd clients/kpidash-client
ruff format --check .
ruff check .
ty check
pytest -q
```

---

## 5. Publish a Service Status Card

After sprint 006 lands, the existing `clients/kpidash-client` CLI grows a `service-status` subcommand. Usage:

```bash
# OK
uv run kpidash-client service-status \
    --name ingest --state ok --text "queue 0"

# Unhealthy, with host annotation and icon
uv run kpidash-client service-status \
    --name ingest --state unhealthy --text "queue 12k" --host kai --icon 3

# Maintenance / down — same shape
uv run kpidash-client service-status --name ingest --state maintenance --text "deploy"
uv run kpidash-client service-status --name ingest --state down --text "stopped"
```

Equivalent manual operator call (no client install needed):

```bash
redis-cli SET kpidash:services:ingest \
  '{"ts":'"$(date +%s)"',"state":"ok","text":"queue 0"}'
```

Expected behaviour:
- A green card appears in the footer within 2 s of the first valid `ok` payload (SC-006).
- If publishing stops while in `ok` / `unhealthy` / `maintenance`, the card border transitions to red 60–62 s after the payload's `ts` (SC-007).
- A `down` card stays gray indefinitely as long as `down` is the most recent payload (SC-008).
- A malformed payload (missing `state` or unrecognised `state`) is ignored (FR-022a); the card retains its previous valid state and continues aging out normally.

## 6. Configure graph reporting from `kai` (FR-032)

`kpidash-client` is already deployed on `kai`. To enable graph samples (sprint 006 adds the required `host` field to the existing dev-telemetry payload, so once the client is upgraded, no toggle is needed on the dashboard side):

1. Upgrade `kpidash-client` on `kai` to the sprint-006 release (it now writes `host` in each dev-telemetry payload).
2. Confirm dev telemetry is enabled in the client config (TOML, typically at `~/.config/kpidash-client/config.toml`):

   ```toml
   [client]
   dev_interval_s = 1.0
   ```

3. Restart the client daemon on `kai`:

   ```bash
   ssh kai 'systemctl --user restart kpidash-client'
   ```

4. On the dashboard, a Graph widget labelled `kai` should appear in rows 2-3 within one poll cycle, placed alphabetically (so `kai` before `kdash` if both are reporting — FR-013).
5. If publishing stops, the "NO NEW DATA" overlay should appear within ≤ 35 s (SC-003), and the widget persists until the next dashboard restart (FR-015a).

## 7. Troubleshooting

| Symptom | Likely cause | Fix |
|---------|--------------|-----|
| Footer empty | No keys match `kpidash:services:*`, or all payloads malformed | `redis-cli --scan --pattern 'kpidash:services:*'`; inspect a value; verify required fields `ts`, `state`, `text`. |
| Card stays gray when service is up | Most recent payload has `state: down` | Publish a fresh `ok` payload; gray is sticky for `down` by design (FR-019). |
| Graph shows "NO NEW DATA" immediately on a fresh deploy | Host's last sample was > 30 s ago at startup | Wait for the next client publish cycle; overlay clears when a new sample arrives. |
| `kpidash-mockup` target not built | Building under cross-compile or `TESTS_ONLY=ON` | Use the native `build/` dir (FR-031). |
| Icon missing on card | `icon` index not in registry | Re-publish with a known index, or extend `src/icons.c` and rebuild. |
