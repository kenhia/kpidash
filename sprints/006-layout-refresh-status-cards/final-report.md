# Sprint 006 — Final Report

## Ship-gate results

- **clang-format**: 0 files with diffs (dry-run -Werror clean on all touched files).
- **cppcheck**: 0 warnings (`--enable=warning` on `src/registry.c`, `src/redis.c`, `src/ui.c`, `src/widgets/service_card.c`, `src/widgets/dev_graph.c`).
- **Native build**: clean. `ctest --test-dir build` → **8/8 passing** (`test_config`, `test_layout`, `test_redis_json`, `test_service_card`, `test_graph_router`, `test_icons`, `test_layout_pool`, `test_widget_leak`).
- **Pi cross build** (`build-pi`, aarch64): clean. Only acceptable noise is the pre-existing `ld: warning: ... requires executable stack` originating from `src/redis.c.o` (carried from sprint 005).
- **Python client** (`clients/kpidash-client`): `uv run pytest -q` → **60/60 passing**; `uv run ruff check kpidash_client tests` → clean.

## Deferred items

The following tasks are marked `[~]` (deferred) and require follow-up:

| Task | Reason |
|------|--------|
| T016, T032, T038 | Pi-hardware visual verification — pending operator soak on rpi53. |
| T040 | **CUT** — superseded by live-iteration loop on rpi53 using seeded `redis-cli SET kpidash:services:*` entries (see `/memories/repo/kpidash-deploy.md`). `src/mockup_main.c` remains as a stdio stub. |
| T041 | Fortune font final visual selection — awaits operator sign-off on Pi (current: Montserrat Bold 36). |
| T042 | Dynamic fortune sizing (stretch goal). |
| T045 (hardware portion) | 30-minute soak + visual diff of status/gutter area against `main` — pending Pi deploy. |

## Operator subjective coherence (SC-011)

Deferred to operator visual sign-off on rpi53.

## Operator notes (live observation 2026-05-23)

- Two graphs render on rpi53: `kubs0` (host field present) and `(legacy)` (kubsdb's payload — `ram_total_mb≈64240` ≈ 62.7 GB, `gpu: null`).
- Root cause: the upstream Python client publishes `"hostname"` while the C parser reads `"host"`. T037 adds `"host"` alongside, but only the `kubs0` daemon has been refreshed to the post-T037 client. To bring kai and kubsdb out of `(legacy)`: `cd clients/kpidash-client && uv tool install --reinstall .` on each host, then `systemctl --user restart kpidash-client`.
- To disable graph publishing for a host (without removing its Row 1 client card): comment out `dev_interval_s` in that host's client config. The dev-telemetry loop only starts when `dev_interval_s` is set (see `clients/kpidash-client/kpidash_client/daemon.py` L105–L108). `telemetry_interval_s` drives the slower per-client telemetry that backs the client card and should be left alone.

## What landed

- New Redis namespace `kpidash:services:<name>` (no TTL) with full parse + freshness logic; SCAN poll added to `redis_poll`.
- Service registry (`SERVICE_REGISTRY_MAX=32`, pthread-mutex) with sticky-GRAY for `DOWN`, 60 s freshness window for non-final states.
- Service card widget (220×240, 6 px coloured border, icon + name + text); rendered into the footer strip; one card per Redis key.
- Per-host graph router (`GRAPH_HOST_MAX=8`, `GRAPH_HOST_STALE_SECONDS=30.0`, no eviction); `dev_graph_set_stale()` overlay banner ("NO NEW DATA") wired into `ui.c`.
- `dev_telemetry` parser now reads `host` (default `"(legacy)"`); samples are routed per host.
- **Multi-host graph UI (T035)**: per-host `dev_telemetry_t` lives on `graph_host_series_t.telemetry`; `redis.c` Step 8 writes each parsed sample into its host's slot; `ui.c` snapshots all hosts, sorts alphabetically, builds N `WIDGET_GRAPH` requests with `payload = host`, and creates/positions/updates a `dev_graph` widget per host via the series's `widget` slot. Pool overflow (>12 cells) hides extra widgets. Legacy single-host `g_dev_graph`/`g_graph_client` globals retired.
- **GPU trace thickening (T036)**: triple-trace trick — for the two GPU series (compute % and VRAM MB), two flanking series (`+offset` / `-offset`) share the original colour. Offsets: `GPU_PCT_OFFSET = 1` axis unit (≈0.4 %); `GPU_VRAM_OFFSET_DIV = 240` → `offset_mb = vram_max / 240`. Visually merges into a ~2.5× band. Documented in `src/widgets/dev_graph.c`.
- Python client `set_service_status` API + `kpidash-client service-status` CLI subcommand; client now publishes `host` in dev-telemetry payloads.
- Documentation: `docs/CLIENT-PROTOCOL.md` §8a (Service Status) and `host` field added to §8; `docs/ARCHITECTURE.md` Sprint 006 section appended.
- Tests: `tests/test_service_card.c` (8 groups: parse + colour truth table + malformed payloads) and `tests/test_graph_router.c` (stable pointer, touch, stale boundary, sentinel, no-eviction, multi-host, NULL/empty, alpha ordering).
