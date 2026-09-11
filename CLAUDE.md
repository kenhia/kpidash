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
  - projects that deploy end the record with a `## Deployed` section:
    what shipped, where, when, and what was verified live — appended
    after the deploy, not predicted before it
- `docs/` — project documentation, architecture, usage
- `.scratch/` — git-ignored scratch space for user or agent ephemera;
  use it instead of /tmp
- `justfile` — dev recipes; default recipe is `@just --list`; `just check`
  runs the CI gates; `just deploy` (or variants) if the project deploys
- `.env` — git-ignored; tokens and environment vars

### Workflow

- One sprint ≈ one PR. Sprint proposals and work items are managed in
  `korg`; durable cross-project knowledge goes in `klams`.
- Mark each work item resolved as its work completes — don't batch the
  resolutions into sprint-ship. A proposal's progress should be readable
  while the sprint is running, which is the only time it is useful.
- If the korg or klams MCP tools are unavailable in your session, say so
  up front — don't silently work around missing infrastructure.
- A few projects share contract surfaces with siblings and have a
  **guiding plan** constraining how those change; most have none, and one
  grep is the whole cost of finding out. Grep the `index.md` routing
  table in `kai:~/src/tools/cross-project-planning` — a local path on
  kai, read through kaed from any other host (`root: "kai:src"`, path
  `tools/cross-project-planning/…`); don't clone a second copy. Not
  listed → nothing applies. Listed → read the mapped plan folder before
  planning sessions and before changing a contract surface it names, and
  amend the plan in the same ship when what you build diverges from it.
- TDD preferred: write the failing test first when practical.

### Tooling preferences

- C/C++ built with `cmake`: configure out-of-source, build with
  `cmake --build`, test with `ctest`
- `ctest` prints "No tests were found!!!" and **exits 0** when nothing is
  registered — pass `--no-tests=error` (CMake ≥ 3.20) or the gate passes
  loudest when there is least to check (same trap as `gofmt -l`)
- The configure flags are the project's own. If the repo documents a
  tests-only or native-CI mode, the gate uses that — a gate needing a
  cross-compiler, a sysroot or hardware is a gate nobody runs
- No formatter in the gate: `clang-format` asserts nothing without a
  committed `.clang-format`. Add `just fmt` once the repo has one
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

- **`just check`** — the gate: `check-dashboard` (configure `-DTESTS_ONLY=ON`,
  build, `ctest --no-tests=error`), `check-release` (the same sources at `-O2`
  with `-Werror`), then `check-client` (ruff + pytest). `TESTS_ONLY` skips the
  dashboard binary and its LVGL/DRM deps, so it needs no Pi and no sysroot.
  This is the only automated safety net in the repo — there is no CI.
  - It needs **one** submodule — `lib/kdashdata`, for the shared band
    classifier `src/registry.c` calls. The gate inits it itself, so a clean
    clone still passes in one command. `lib/lvgl` is still not needed.
  - `--no-tests=error` matters: bare `ctest` prints "No tests were found!!!"
    and **exits 0**, so without it the gate would pass loudest when there is
    least to check.
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
- **Two git submodules.** Clone with `--recurse-submodules`. `lib/lvgl` is the
  big one and only a Pi/native build needs it. `lib/kdashdata` (sprint 018,
  WI #2244) is small and **the gate does need it** — `src/registry.c` calls
  `kdash_apttemps_band()`, and registry.c compiles into six test targets.
  kpidash links `kdash_core` only: the pure logic, no sockets, no hiredis —
  the I/O shell (`kdash`) is deliberately not linked, because kpidash has its
  own Redis layer and is not migrating it.
- **The apartment-temperature thresholds are not in this repo any more.**
  65/75/80 °F live in kdashdata (`KDASH_APTTEMPS_{COLD,OK,HOT}_F`);
  `APTTEMPS_COLOR_*` in `src/registry.h` is the *palette* only (CD-10: the
  library carries no colours). Changing a number in registry.h changes
  nothing — change it in kdashdata.
- **Build trees are git-ignored** (`build-*/`): `build-tests/` is the gate's,
  `build-pi5/` the cross-build, `build-native/` a local full build. Note
  `scripts/deploy.sh` uses its own `build-pi/`.
- **`-Wall -Wextra` is on for every target, and what "warning-clean" covers is
  now a precise claim** (WI #1934). It used to be an unqualified one, and it
  was false under the flags that actually reach the Pi: `scripts/deploy.sh`
  builds `-DCMAKE_BUILD_TYPE=Release`, and GCC's flow-sensitive warnings
  (`-Wstringop-truncation`, `-Wunused-result`, the `-Wmaybe-*` family) only run
  once the optimiser does. Two warnings survived unseen that way until sprint
  017 read the deploy output.
  - `just check-release` now compiles at `-O2 -Werror` — but **only what
    TESTS_ONLY compiles**: `config.c`, `redis.c`, `registry.c`, `icons.c`,
    `memstat.c`.
  - The dashboard-only sources (`main.c`, `ui.c`, `fortune.c`, `screenshot.c`,
    `src/widgets/*`) need LVGL and libpng, and **kai has no libpng-dev**, so
    they cannot be compiled on this dev host at all. They stay covered only by
    the deploy's own Release build — read its output.
  - So: warning-clean at `-O2` for the tested core, gated; warning-clean for
    the rest by inspection at deploy time, not gated. Keep both that way.
- **`clang-format` and `cppcheck` are not installed on kai**, so neither is in the
  gate, though `.clang-format` exists and the retired Spec-Kit constitution
  claimed both as pre-commit checks. Format new C to match surrounding style by
  hand.
- GPU telemetry is NVIDIA-only (via `pynvml`); Windows client disk types come
  from user config rather than detection.
