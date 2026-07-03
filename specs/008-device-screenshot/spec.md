# Sprint 008 — Device Self-Screenshot

**Branch**: `008-device-screenshot`
**Status**: Implemented

## Problem

kpidash renders to the Pi's panel over DRM/KMS. There is no way to see the
dashboard remotely — to debug layout or the dev graphs an agent (or a human off
site) has to photograph the glossy screen, which is imprecise and often
impossible. The telemetry itself lives in Redis, but the *rendered* result
(fonts, colours, graph shapes, overflow) can only be judged from actual pixels.

Because it renders with DRM/KMS (not X11/Wayland) there is no external
framebuffer grab; the capture has to happen inside the process that owns the
DRM master.

## Goal

A one-shot, pixel-perfect device self-screenshot triggered over the existing
Redis channel, plus a tiny client CLI (`krpidss`) so both agents and the user
can grab a PNG of the live dashboard with a single command.

This mirrors the proven kdeskdash feature (`kdeskdash:screenshot` + `kddss`);
kpidash uses the same LVGL 9 / DRM stack, so the capture path ports directly.
kdeskdash has a dedicated control surface; kpidash instead reuses its 1 s Redis
poll as the control surface (no new connection or thread).

## Design decisions

- **Control key**: `kpidash:screenshot`, consumed with `GETDEL` inside
  `redis_poll()` (one-shot; the key is deleted as it is read). A value starting
  with `/` names the output path; anything else uses the default. Runs on the
  UI thread, which is required for `lv_snapshot_take`.
- **Capture**: `lv_snapshot_take(lv_screen_active(), LV_COLOR_FORMAT_XRGB8888)`
  re-renders the active screen to a fresh buffer independent of the DRM
  back-buffer — backend-agnostic and safe. Requires `LV_USE_SNAPSHOT 1`
  (added to `lv_conf.h`).
- **Encoding**: pure 24-bit BMP written on the device (`bmp_write.c`, no LVGL /
  Redis deps, host-testable). BMP keeps the device side trivial; the client
  converts to PNG. Matches kddss so tooling stays symmetric.
- **Default output**: `/tmp/kpidash-shot.bmp` (`SCREENSHOT_DEFAULT_PATH`). The
  service runs as root and overwrites this file in place; the client judges
  freshness by mtime so a stale file from a prior run never satisfies the wait.
- **Client CLI** `scripts/krpidss` → `~/.local/bin/krpidss`: sets the key,
  waits for a fresh BMP, streams it back over one SSH round-trip, converts to
  PNG locally (PIL or ffmpeg, BMP fallback). Unlike kddss, rpi53's Redis
  **requires auth**, so the remote snippet sources `REDISCLI_AUTH` from
  `~/.config/kpidash-client/redis-auth.env` (redis-cli reads that var
  automatically). Host override via `KRP_HOST` (default `ken@rpi53`).
- **Test seam**: `redis.c` is compiled into two lightweight `KPIDASH_TEST_STUBS`
  test targets that don't link LVGL. Following the existing convention (see the
  `redis_poll_services` stub), each provides a stub `screenshot_save`.

## Files

- `src/bmp_write.{c,h}` — pure XRGB8888 → 24-bit BMP encoder.
- `src/screenshot.{c,h}` — LVGL snapshot glue → BMP.
- `src/redis.c` — `GETDEL kpidash:screenshot` → `screenshot_save()` (Step 10).
- `src/protocol.h` — `KPIDASH_KEY_SCREENSHOT`.
- `lv_conf.h` — `LV_USE_SNAPSHOT 1`.
- `CMakeLists.txt` — add the two sources.
- `scripts/krpidss` — client CLI.
- `tests/test_redis_json.c`, `tests/test_service_card.c` — `screenshot_save` stub.

## Verification

- Cross-compiled aarch64 binary links `screenshot_save`, `bmp_write_xrgb8888`,
  `lv_snapshot_take`; deployed to rpi53; service healthy (0 restarts).
- `krpidss` produced a 3840×2160 PNG of the live dashboard end-to-end.
- `ctest` (TESTS_ONLY): 7/7 pass.

## Build note

Cross-compiling on kai required completing `~/pi5-sysroot` with the `-dev` files
for libpng, zlib, libcjson, and hiredis (headers, `.pc`, and `.so` symlinks),
rsynced from rpi53. See `docs/HANDOFF-CROSSCOMPILE.md`.

## Follow-ups (not in this sprint)

- A `bmp_write` host unit test (kdeskdash has none either; encoder is simple).
- Optionally deploy `krpidss` to other operator machines' `~/.local/bin`.
