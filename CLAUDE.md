<!-- kproject:begin — managed by kprojects; do not edit inside this block -->
## kproject conventions

This project uses the kproject minimal harness
(<https://github.com/kenhia/kprojects>). Keep context small; prefer doing
over ceremony.

### Layout

- `sprints/` — the project's evolution, one record per PR-sized unit of
  work (a "sprint")
  - `planning/` — planning docs; at minimum `roadmap.md` (the general plan)
  - `review/` — more formal reviews as the project matures
  - sprint records: `###-<short-name>.md` for small projects, or a
    `###-<short-name>/` directory of files for larger/more formal ones
  - a sprint record is one informal narrative: goal, decisions, what
    shipped, follow-ups — written during the sprint, not after
- `docs/` — project documentation, architecture, usage
- `.scratch/` — git-ignored scratch space for user or agent ephemera;
  use it instead of /tmp
- `justfile` — dev recipes; default recipe is `@just --list`; `just check`
  runs the CI gates; `just deploy` (or variants) if the project deploys
- `.env` — git-ignored; tokens and environment vars

### Workflow

- One sprint ≈ one PR. Sprint proposals and work items are managed in
  `korg`; durable cross-project knowledge goes in `klams`.
- If the korg or klams MCP tools are unavailable in your session, say so
  up front — don't silently work around missing infrastructure.
- TDD preferred: write the failing test first when practical.

### Tooling preferences

- License is MIT unless specifically directed otherwise
<!-- kproject:end -->

## Project

kpidash is a fullscreen KPI dashboard for a Raspberry Pi 5 touch panel (`rpi53`),
written in C11 with [LVGL](https://lvgl.io/) 9.2.2 rendering straight to DRM/KMS —
no X11, no Wayland, no window chrome. Client machines push telemetry, health,
activity and repo status into a Redis instance on the Pi; the dashboard polls
Redis once a second and re-renders. `clients/` holds two Python 3.13 packages
that feed it: `kpidash-client` (the telemetry daemon + CLI) and `kpidash-mcp`
(an MCP server for agent activity reporting).

### Build, test, deploy

- **`just check`** — the gate: configures with `-DTESTS_ONLY=ON`, builds, runs
  `ctest`. `TESTS_ONLY` skips the dashboard binary and its LVGL/DRM deps, so it
  needs no Pi, no sysroot and no submodule checkout. This is the only automated
  safety net in the repo — there is no CI.
- **Pi build** — cross-compiled x86_64 → aarch64 against a synced Pi sysroot:
  `cmake -B build-pi5 -DCMAKE_TOOLCHAIN_FILE=cmake/aarch64-toolchain.cmake`.
  The sysroot rsync and toolchain setup are in `docs/HANDOFF-CROSSCOMPILE.md`.
  It cannot be gated on a dev host, so a Pi build is verified by deploying.
- **Deploy** — `scripts/deploy.sh` (cross-builds, stages through `/tmp`, installs
  atomically, restarts the service, then reads back the version the running
  binary self-reports). `--rollback` restores the previous binary; `--no-build`
  ships what is already built. Prefer it over the `deploy` CMake target, which
  is the older stop/scp/start pattern.
- **`just publish`** — builds `clients/kpidash-client` and adds it to the homelab
  package store. `kpkg` refuses an already-published version, so bump
  `clients/kpidash-client/pyproject.toml` first.

### Read these first

- `docs/ARCHITECTURE.md` — system design and component relationships
- `docs/CLIENT-PROTOCOL.md` — the Redis key/value schema, the contract between
  the dashboard and every client
- `src/protocol.h` — Redis key macros and compile-time limits
- `CMakeLists.txt` — the `TESTS_ONLY` split is the thing to understand before
  touching the build
- `sprints/planning/roadmap.md` — where this is going

### Conventions and gotchas

- **No input devices.** The panel has no mouse or keyboard, so nothing in the UI
  may be scrollable or require interaction to reveal content. Everything must be
  legible at a glance from across the room.
- **The Redis poll runs on the LVGL thread** as a synchronous 1s cycle. Anything
  slow added there stalls the render loop.
- **Redis auth is `REDISCLI_AUTH` in the environment only** — never in code, never
  in a config file. DRM needs root, and `sudo -E` is what preserves the variable.
- **Redis keys** are `kpidash:{category}:{hostname}:{subcategory}`, lowercase with
  colons; JSON values use `snake_case` fields.
- **`lib/lvgl` is a git submodule.** Clone with `--recurse-submodules`. `just check`
  does not need it; a Pi build does.
- **Build trees are git-ignored** (`build-*/`): `build-tests/` is the gate's,
  `build-pi5/` the cross-build, `build-native/` a local full build. Note
  `scripts/deploy.sh` uses its own `build-pi/`.
- `-Wall -Wextra` is on for every target and the tree is warning-clean — keep it
  that way.
- **`clang-format` and `cppcheck` are not installed on kai**, so neither is in the
  gate, though `.clang-format` exists and the retired Spec-Kit constitution
  claimed both as pre-commit checks. Format new C to match surrounding style by
  hand.
- GPU telemetry is NVIDIA-only (via `pynvml`); Windows client disk types come
  from user config rather than detection.
