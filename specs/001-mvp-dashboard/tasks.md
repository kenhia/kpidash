---
description: "Task list for kpidash MVP — Multi-Client Status Dashboard"
---

# Tasks: kpidash MVP — Multi-Client Status Dashboard

**Input**: Design documents from `/specs/001-mvp-dashboard/`
**Prerequisites**: plan.md ✅, spec.md ✅, research.md ✅, data-model.md ✅, contracts/redis-schema.md ✅, contracts/mcp-tools.md ✅, quickstart.md ✅

**Organization**: Tasks grouped by user story for independent implementation and testing.
**Tests**: C unit tests included for testable modules (config.c, redis.c JSON parsing; see T010a, T010b); hardware-layer C code exempt per constitution Principle III; Python logic tests noted per task.

## Format: `[ID] [P?] [Story?] Description`

- **[P]**: Can run in parallel (different files, no incomplete dependencies)
- **[Story]**: Which user story this belongs to (US1–US9)
- Exact file paths included in every task

---

## Phase 1: Setup (Shared Infrastructure)

**Purpose**: Repository scaffolding, build system, and shared code foundations

- [X] T001 Remove POC-only code: delete `src/net.c`, `src/net.h`; remove UDP socket code from `src/main.c`; remove title bar rendering from `src/ui.c` (keep `src/ui.c` stub for later widget integration)
- [X] T002 [P] Add hiredis to `CMakeLists.txt`: `find_package(hiredis REQUIRED)` and link `hiredis::hiredis` to the `kpidash` target
- [X] T003 [P] Create `.clang-format` at repo root with Google style base and 4-space indent override
- [X] T004 [P] Create `clients/kpidash-client/pyproject.toml` with `[project]` metadata, dependencies (`psutil>=6`, `pynvml>=11`, `redis>=5`, `GitPython>=3.1`, `click>=8`), and `[project.scripts]` entry `kpidash-client = "kpidash_client.cli:main"`
- [X] T005 [P] Create `clients/kpidash-mcp/pyproject.toml` with `[project]` metadata, dependencies (`mcp>=1`, `redis>=5`), and `[project.scripts]` entry `kpidash-mcp = "kpidash_mcp.server:main"`
- [X] T006 [P] Create `clients/kpidash-client/kpidash_client/__init__.py` and `clients/kpidash-mcp/kpidash_mcp/__init__.py` (empty init files)
- [X] T007 Create `src/protocol.h`: define all Redis key template macros (`KPIDASH_KEY_HEALTH`, `KPIDASH_KEY_TELEMETRY`, `KPIDASH_KEY_CLIENTS`, `KPIDASH_KEY_ACTIVITIES`, `KPIDASH_KEY_ACTIVITY`, `KPIDASH_KEY_REPOS`, `KPIDASH_KEY_FORTUNE_CURRENT`, `KPIDASH_KEY_FORTUNE_PUSHED`, `KPIDASH_KEY_STATUS_CURRENT`, `KPIDASH_KEY_STATUS_ACK`, `KPIDASH_KEY_LOGPATH`, `KPIDASH_KEY_VERSION`); define TTL constants and limits (`MAX_CLIENTS 16`, `MAX_DISKS 8`, `ACTIVITY_MAX_DISPLAY 10`, `HEALTH_TTL_S 5`, `TELEMETRY_TTL_S 15`, `REPOS_TTL_S 30`, `FORTUNE_INTERVAL_S 300`)

---

## Phase 2: Foundational (Blocking Prerequisites)

**Purpose**: Core infrastructure that ALL user story phases depend on

**⚠️ CRITICAL**: No user story work begins until this phase is complete

- [X] T008 Extend `src/registry.h` and `src/registry.c` to use the `client_info_t` struct defined in `data-model.md` (add `gpu_info_t`, `disk_entry_t[]`, `online` flag, `telemetry_ts`); retain existing mutex-protection pattern
- [X] T009 Create `src/config.h` and `src/config.c`: parse environment variables `REDISCLI_AUTH`, `KPIDASH_DRM_DEV` (default `/dev/dri/card1`), `KPIDASH_REDIS_HOST` (default `127.0.0.1`), `KPIDASH_REDIS_PORT` (default `6379`), `KPIDASH_MAX_CLIENTS` (default `16`), `KPIDASH_ACTIVITY_MAX` (default `10`); expose a `kpidash_config_t` struct and `config_load()` function; fortune rotation interval is the compile-time constant `FORTUNE_INTERVAL_S` (300 s, from `protocol.h`) — not an env var (A2)
- [X] T010 Create `src/redis.h` and `src/redis.c`: implement `redis_connect()` with `REDISCLI_AUTH` auth, `redis_disconnect()`, `redis_poll()` (non-blocking 1 ms timeout full poll cycle matching the access patterns in `contracts/redis-schema.md`), and `redis_reconnect_if_needed()`; `redis_poll()` updates the `client_info_t` registry in place using cJSON for JSON parsing
- [X] T010a [P] Add `tests/` directory to `CMakeLists.txt`; create `tests/test_config.c`: test `config_load()` with `setenv()`/`unsetenv()` covering all supported env vars, their default values, and override values; verify resulting `kpidash_config_t` fields; register with `add_test()` so `ctest` runs it on native host without hardware (U1)
- [X] T010b [P] Create `tests/test_redis_json.c`: test the cJSON parsing logic in `redis_poll()` by calling internal parse helpers with hand-crafted JSON strings matching schemas in `contracts/redis-schema.md`; verify `client_info_t` fields for health (online/offline), telemetry (GPU present and absent), and activity payloads; register with `add_test()` (ctest, no hardware required) (U1)
- [X] T011 [P] Create `clients/kpidash-client/kpidash_client/config.py`: implement `ClientConfig` dataclass loaded from `~/.config/kpidash-client/config.toml` (XDG on Linux, `%APPDATA%` on Windows) using `tomllib`; fields match the schema in `research.md` Decision 5; MUST include `telemetry_interval_s` (int, default `5`) for the daemon polling interval (A1); validate required fields on load; raise `ConfigError` with clear message if invalid
- [X] T012 [P] Create `clients/kpidash-client/kpidash_client/redis_client.py`: implement `RedisClient` wrapping `redis.Redis`; reads host/port from `ClientConfig`, password from `REDISCLI_AUTH` env var; provides `connect()`, `disconnect()`, `reconnect_on_failure()` decorator/context; exposes typed helper methods for each write operation defined in `contracts/redis-schema.md` (`write_health()`, `write_telemetry()`, `write_repos()`, `start_activity()`, `end_activity()`, `push_fortune()`, `get_status_current()`, `ack_status()`, `get_logpath()`); raises `RedisClientError` on connection failure
- [X] T013 [P] Create `clients/kpidash-client/tests/test_config.py`: test `ClientConfig` loading from valid TOML, missing required fields, invalid types, and default values; use `tmp_path` fixture for test config files
- [X] T014 [P] Create `clients/kpidash-client/tests/test_redis_client.py`: test each helper method by mocking `redis.Redis`; verify correct key names (matching `contracts/redis-schema.md`), TTL values, and JSON serialization; test `reconnect_on_failure` retries on `ConnectionError`

**Checkpoint**: Foundation complete — registry, config, and Redis I/O ready for widget and client story work

---

## Phase 3: User Story 1 — At-a-Glance Machine Health (Priority: P1) 🎯 MVP

**Goal**: Dashboard shows client cards with hostname and health indicator (green/red); new clients auto-appear; offline detection via TTL

**Independent Test**: Run `kpidash` on Pi 5; run a minimal health-ping script writing to Redis; confirm green card appears within 10 s; stop the script; confirm card turns red within 5 s

### Implementation for US1

- [X] T015 [US1] Create `src/widgets/client_card.h` and `src/widgets/client_card.c`: LVGL widget displaying hostname label, health LED (green/red), and uptime label; `client_card_create(lv_obj_t *parent, const char *hostname)` returns an `lv_obj_t *`; `client_card_update_health(obj, online, uptime_s)` updates LED color and uptime text; MUST NOT use scroll containers, `lv_list`, or any element requiring pointer interaction (FR-008)
- [X] T016 [US1] Update `src/main.c`: initialize hiredis via `redis_connect()` on startup; register a 1-second LVGL timer calling `redis_poll()` then `ui_refresh()`; handle Redis connection failure at startup by calling `ui_show_redis_error(errmsg)` and entering a timed reconnect retry loop; verify or set `LV_DISP_DEF_REFR_PERIOD 33` in `lv_conf.h` (33 ms ≈ 30 fps, FR-002) if not already configured (D1, A3)
- [X] T017 [US1] Create `src/ui.c` (replace POC stub): implement `ui_init()` creating the root LVGL screen with no title bar; implement `ui_refresh()` that iterates `registry_get_all_clients()`, creates client_card widgets for new hostnames, and calls `client_card_update_health()` for each; remove any cards for hostnames no longer in Redis `kpidash:clients`
- [X] T017a [US1] Add `ui_show_redis_error(const char *msg)` and `ui_hide_redis_error()` to `src/ui.c`/`src/ui.h`: `ui_show_redis_error()` creates/reveals a centered LVGL label displaying "Redis unavailable: {msg}" covering the full screen; MUST NOT use scroll containers or interactive elements (FR-005, FR-008); `ui_hide_redis_error()` hides the overlay; call `ui_show_redis_error()` from `redis_reconnect_if_needed()` when connection is lost and `ui_hide_redis_error()` when restored (C1)
- [X] T018 [US1] Implement health-offline detection in `src/redis.c` `redis_poll()`: after reading `kpidash:client:{h}:health`, set `client_info.online = true/false` based on key existence (key TTL expiry is the offline signal); update `last_seen_ts` and `uptime_seconds` if key exists

**Checkpoint**: US1 complete — client cards appear/disappear and show correct online/offline state

---

## Phase 4: User Story 2 — System Telemetry Per Client (Priority: P2)

**Goal**: Client cards show CPU, RAM, GPU (if present), and disk metrics; Python daemon writes telemetry every 5 s

**Independent Test**: Start daemon on Linux machine; verify dashboard card shows CPU/RAM/GPU/disk values that agree with `htop`/`nvtop` within the telemetry interval

### Implementation for US2 — Dashboard Side

- [X] T019 [P] [US2] Extend `src/widgets/client_card.c`: add `client_card_update_telemetry(obj, client_info_t *)` that updates CPU % label, top-core % label, RAM used/total label, optional GPU section (hidden if `gpu.present == false`), and disk rows (one row per `disk_entry_t`); GPU section and disk rows are created on first non-null telemetry and toggled hidden otherwise
- [X] T020 [P] [US2] Implement telemetry parsing in `src/redis.c` `redis_poll()`: GET `kpidash:client:{h}:telemetry`, parse JSON via cJSON, populate `client_info_t.cpu_pct`, `.top_core_pct`, `.ram_*`, `.gpu`, `.disks[]`, `.disk_count`, `.telemetry_ts`

### Implementation for US2 — Python Client (telemetry collection)

- [X] T021 [P] [US2] Create `clients/kpidash-client/kpidash_client/telemetry/system.py`: implement `collect_system() -> dict` using `psutil.cpu_percent(percpu=True)` for overall and top-core, `psutil.virtual_memory()` for RAM; returns dict matching the `TelemetrySnapshot` JSON schema in `data-model.md`
- [X] T022 [P] [US2] Create `clients/kpidash-client/kpidash_client/telemetry/gpu.py`: implement `collect_gpu() -> dict | None` using `pynvml`; returns GPU name, `vram_used_mb`, `vram_total_mb`, `compute_pct`; returns `None` gracefully if NVML unavailable or no GPU found; uses `pynvml.nvmlInit()` once at import, catches `pynvml.NVMLError`
- [X] T023 [P] [US2] Create `clients/kpidash-client/kpidash_client/telemetry/disk.py`: implement `collect_disks(disk_configs: list[DiskConfig]) -> list[dict]`; for each configured disk calls `psutil.disk_usage(path)` for capacity/usage; on Linux auto-detects `type` via `/sys/block/{dev}/queue/rotational` and device name prefix; on Windows uses `type` from config; returns list of dicts matching `DiskEntry` schema
- [X] T024 [US2] Create `clients/kpidash-client/kpidash_client/daemon.py`: implement `run_daemon(config: ClientConfig)`; on startup calls `redis_client.connect()` and `SADD kpidash:clients {hostname}`; runs a 3-second health-ping loop (`write_health()`) and a `config.telemetry_interval_s`-second telemetry loop (`write_telemetry()`) concurrently using `threading.Timer` or `asyncio`; reconnects on Redis failure; handles SIGTERM/SIGINT for clean shutdown
- [X] T025 [P] [US2] Create `clients/kpidash-client/tests/test_telemetry.py`: unit-test `collect_system()` by mocking `psutil` returns; test `collect_gpu()` with NVML mocked (found and not-found cases); test `collect_disks()` with mocked `psutil.disk_usage` and mocked `/sys/block` paths on Linux

**Checkpoint**: US2 complete — daemon running on Linux machine produces a fully-populated card with telemetry

---

## Phase 5: User Story 3 — Activity Tracking (Priority: P3)

**Goal**: Activities widget shows up to 10 active/recent activities with elapsed timers; any client or MCP server can start/end activities

**Independent Test**: Run `kpidash-client activity start "test task"` on Linux; verify Activities widget shows entry with incrementing elapsed time; run `kpidash-client activity done`; verify widget shows done with total duration

### Implementation for US3 — Dashboard Side

- [X] T026 [P] [US3] Create `src/widgets/activities.h` and `src/widgets/activities.c`: LVGL widget listing up to `ACTIVITY_MAX_DISPLAY` entries; `activities_widget_create(parent)` returns container; `activities_widget_update(obj, activity_list[], count)` redraws rows; each row shows host, name, state badge (active/done), and time (live elapsed for active via LVGL timer, static duration for done); MUST NOT use scroll containers, `lv_list`, or any element requiring pointer interaction (FR-008)
- [X] T027 [P] [US3] Implement activity read in `src/redis.c` `redis_poll()`: `ZREVRANGE kpidash:activities 0 {ACTIVITY_MAX_DISPLAY-1}` to get IDs, then `HGETALL kpidash:activity:{id}` for each; populate an `activity_t[]` array in shared state; handle missing activity keys gracefully

### Implementation for US3 — Python Client CLI

- [X] T028 [P] [US3] Create `clients/kpidash-client/kpidash_client/cli.py`: implement top-level `click` group `kpidash-client` with subgroups `activity`, `status`, `fortune`; wire `activity start <name>` → `redis_client.start_activity()`, `activity done` → `redis_client.end_activity()` (stores last `activity_id` in `~/.config/kpidash-client/state.json`)
- [X] T029 [P] [US3] Create `clients/kpidash-client/tests/test_activity.py`: test `start_activity()` HSET + ZADD + ZREMRANGEBYRANK sequence with mocked Redis; test `end_activity()` sets `state=done` and `end_ts`; test `state.json` persistence of `activity_id`

**Checkpoint**: US3 complete — activities appear in the widget; CLI start/done commands work end-to-end

---

## Phase 6: User Story 4 — Repo Status Awareness (Priority: P4)

**Goal**: Repo Status widget shows repos on non-default branch or with uncommitted changes; client scans on configurable schedule

**Independent Test**: Configure client to watch one repo; switch repo to feature branch; verify widget shows that repo with branch name; switch back; verify it disappears

### Implementation for US4 — Dashboard Side

- [X] T030 [P] [US4] Create `src/widgets/repo_status.h` and `src/widgets/repo_status.c`: LVGL widget showing list of repo entries; `repo_status_widget_create(parent)` returns container; `repo_status_widget_update(obj, repo_list[], count)` redraws rows; each row shows host, repo name, branch, and dirty indicator; if count == 0, shows "all clean" placeholder label; MUST NOT use scroll containers, `lv_list`, or any element requiring pointer interaction (FR-008)
- [X] T031 [P] [US4] Implement repo read in `src/redis.c` `redis_poll()`: `SMEMBERS kpidash:clients`, then for each hostname `HGETALL kpidash:repos:{hostname}`; parse each field JSON value; populate `repo_entry_t[]` array in shared state; skip clients with no repos key

### Implementation for US4 — Python Client

- [X] T032 [P] [US4] Create `clients/kpidash-client/kpidash_client/repos.py`: implement `scan_repos(repo_config: RepoConfig) -> list[RepoStatus]`; resolves explicit paths + scans `scan_roots` up to `scan_depth` for `.git` dirs, excluding `exclude` paths; for each repo: uses GitPython `Repo(path)` to get `active_branch.name`, `is_dirty(untracked_files=True)`, determines `default_branch` (tries `origin/HEAD`, falls back to `main`/`master`); returns only repos where `branch != default_branch OR is_dirty`
- [X] T033 [US4] Add repo scan loop to `clients/kpidash-client/kpidash_client/daemon.py`: run `scan_repos()` every 30 seconds; call `redis_client.write_repos()` for dirty/non-default results; call `HDEL` for repos that returned to clean state; set key TTL 30 s on the hash
- [X] T034 [P] [US4] Create `clients/kpidash-client/tests/test_repos.py`: test `scan_repos()` with mocked GitPython; test explicit path handling; test scan_roots depth limiting; test exclude path filtering; test that clean repos on default branch are not returned

**Checkpoint**: US4 complete — repo widget populates from client scans; clean repos disappear automatically

---

## Phase 7: User Story 5 — Linux Client Setup (Priority: P5)

**Goal**: Complete, installable Linux client package with daemon autostart and full CLI

**Independent Test**: Fresh-install the package on a Linux machine, configure TOML, run daemon — dashboard card appears with telemetry within 10 s

### Implementation for US5

- [X] T035 [US5] Implement `kpidash-client daemon start` and `daemon stop` CLI commands in `clients/kpidash-client/kpidash_client/cli.py`: `start` forks the daemon process (or runs in foreground with `--foreground`); writes PID to `~/.config/kpidash-client/daemon.pid`; `stop` sends SIGTERM to the stored PID
- [X] T036 [P] [US5] Add `clients/kpidash-client/kpidash_client/telemetry/disk.py` Linux auto-detection for disk medium type: read `/sys/block/{dev}/queue/rotational` (0=SSD/NVMe, 1=HDD); detect NVMe by checking if device path contains `nvme`; fall back to config `type` if auto-detection fails
- [X] T037 [P] [US5] Write `clients/kpidash-client/README.md`: installation steps (`pip install .`), config file location and full example (matching `quickstart.md`), environment variable (`REDISCLI_AUTH`), daemon commands
- [X] T038 [US5] Verify `clients/kpidash-client` installs cleanly on Linux with `pip install -e .` and all CLI entry points are functional; run `ruff format --check . && ruff check . && ty check && pytest -q`

**Checkpoint**: US5 complete — Linux client is a full installable package with documented setup

---

## Phase 8: User Story 6 — Windows Client Setup (Priority: P6)

**Goal**: `kpidash-client` Python package runs on Windows; health, telemetry, and activities work; user starts it manually

**Independent Test**: Install on Windows; run `python -m kpidash_client.cli daemon start`; dashboard card appears with telemetry within 10 s

### Implementation for US6

- [X] T039 [US6] Add Windows-specific config path in `clients/kpidash-client/kpidash_client/config.py`: use `%APPDATA%\kpidash-client\config.toml` on Windows (`platform.system() == "Windows"`); document in `README.md`
- [X] T040 [P] [US6] Verify `clients/kpidash-client/kpidash_client/telemetry/disk.py` Windows path: disk `type` comes from config (no auto-detection on Windows); `psutil.disk_usage()` works cross-platform; add guard for Linux-only `/sys/block` code path
- [X] T041 [P] [US6] Verify `clients/kpidash-client/kpidash_client/telemetry/gpu.py` on Windows: `pynvml` works on Windows with NVIDIA drivers; add `try/except` to catch driver-not-loaded scenario and return `None`
- [X] T042 [US6] Add `clients/kpidash-client/README.md` Windows section: installing Python 3.11+, `pip install .`, setting `REDISCLI_AUTH` in PowerShell, running `kpidash-client daemon start` (foreground mode for MVP)

**Checkpoint**: US6 complete — Windows clients appear on dashboard from manual process launch

---

## Phase 9: User Story 7 — Agent Activity Reporting via MCP Server (Priority: P7)

**Goal**: AI agents can call `start_activity` and `end_activity` MCP tools; activities appear on dashboard identically to CLI-started ones

**Independent Test**: Add `kpidash-mcp` to an AI agent's config; invoke agent on a task; confirm activity appears in dashboard Activities widget

### Implementation for US7

- [X] T043 [P] [US7] Create `clients/kpidash-mcp/kpidash_mcp/redis_client.py`: minimal Redis helper (connect with `KPIDASH_REDIS_HOST`, `KPIDASH_REDIS_PORT`, `REDISCLI_AUTH`); implement `start_activity(name, host) -> dict` and `end_activity(activity_id) -> dict` matching the schemas in `contracts/mcp-tools.md`; raise `MCPRedisError` on failure
- [X] T044 [P] [US7] Create `clients/kpidash-mcp/kpidash_mcp/tools.py`: implement `@mcp.tool()` decorated functions `start_activity` and `end_activity` per the input schemas and return shapes in `contracts/mcp-tools.md`; wrap Redis errors in MCP error results (`is_error=True`)
- [X] T045 [US7] Create `clients/kpidash-mcp/kpidash_mcp/server.py`: instantiate `mcp.Server("kpidash")`, register tools from `tools.py`, implement `main()` entry point running stdio transport; add `__main__` guard
- [X] T046 [P] [US7] Create `clients/kpidash-mcp/tests/test_tools.py`: test `start_activity` and `end_activity` with mocked Redis; verify correct Redis key writes (HSET + ZADD + ZREMRANGEBYRANK for start; HSET for end); verify error result returned on Redis failure
- [X] T047 [US7] Write `clients/kpidash-mcp/README.md`: MCP server configuration examples for Claude Desktop and GitHub Copilot (matching `quickstart.md`), required environment variables

**Checkpoint**: US7 complete — AI agents can report activities via `kpidash-mcp`

---

## Phase 10: User Story 8 — Dashboard Status and Log Access (Priority: P8)

**Goal**: Status bar appears at bottom when warnings/errors occur; clients can acknowledge; log path retrievable via CLI

**Independent Test**: Trigger a dashboard warning (e.g., cJSON parse failure); verify status bar appears; run `kpidash-client status ack`; verify bar disappears; run `kpidash-client log-path`; verify a valid path is returned

### Implementation for US8 — Dashboard Side

- [X] T048 [P] [US8] Create `src/status.h` and `src/status.c`: implement in-memory FIFO queue of `status_message_t`; `status_push(severity, message)` adds to queue; `status_get_current()` returns current head; `status_ack(message_id)` removes current and promotes next; write current to `kpidash:status:current` on change; DEL key when queue empty; check `kpidash:status:ack:{id}` during each Redis poll cycle
- [X] T049 [P] [US8] Create `src/widgets/status_bar.h` and `src/widgets/status_bar.c`: LVGL label widget pinned to bottom of screen; `status_bar_create(parent)` creates hidden label; `status_bar_show(severity, message)` makes it visible with appropriate color (yellow=warning, red=error); `status_bar_hide()` hides it; widget occupies no layout space when hidden; MUST NOT use scroll containers or interactive elements (FR-008)
- [X] T050 [US8] Write `kpidash:system:logpath` in `src/main.c` on startup: determine log file path (from `KPIDASH_LOG_FILE` env var, default `/var/log/kpidash/kpidash.log`); `SET kpidash:system:logpath {path}` via hiredis; also write `kpidash:system:version "1.0.0"`

### Implementation for US8 — Python Client CLI

- [X] T051 [P] [US8] Add `status ack` and `log-path` commands to `clients/kpidash-client/kpidash_client/cli.py`: `status ack` calls `redis_client.get_status_current()` to read `message_id`, then `redis_client.ack_status(message_id)` (`SET kpidash:status:ack:{id} 1 EX 60`); `log-path` calls `redis_client.get_logpath()` and prints result; both print clear error if Redis is unreachable or key is absent

**Checkpoint**: US8 complete — operational status bar functions; log retrieval works from any client machine

---

## Phase 11: User Story 9 — Fortune Widget (Priority: P9)

**Goal**: Fortune widget rotates `fortune` command output every 5 min; client can push custom fortunes that display immediately

**Independent Test**: Confirm fortune text appears on dashboard; run `kpidash-client fortune push "Hello!"` from a client machine; verify it appears immediately without waiting for rotation; wait 5 min; verify rotation resumes

### Implementation for US9

- [X] T052 [P] [US9] Create `src/widgets/fortune.h` and `src/widgets/fortune.c`: LVGL label widget displaying fortune text; `fortune_widget_create(parent)` creates styled label; `fortune_widget_update(obj, text)` updates displayed text with word-wrap; MUST NOT use scroll containers or interactive elements (FR-008)
- [X] T053 [US9] Implement fortune timer in `src/main.c`: at startup check for `fortune` binary via `access()` (e.g., `/usr/bin/fortune`, `/usr/games/fortune`); if absent, call `status_push(WARNING, "fortune binary not found")` and configure fortune to display randomly-selected canned messages instead of `popen()` output; canned messages: `"Fortune not found. Please shake device vigorously and try again."`, `"Today's fortune is on strike. It demands better working conditions and fewer cows."`, `"Segmentation fault: fortune tried to divide by zen."`, `"No fortune today. The universe is buffering."`, `"Your fortune wandered off. It left a note: 'BRB, consulting the Oracle.'"`, `"404: Wisdom not found. Try enlightenment.exe?"`; register a fixed `FORTUNE_INTERVAL_S` (300 s) LVGL timer that runs `fortune` via `popen()` (or picks a canned message if unavailable), writes to `kpidash:fortune:current`, updates the fortune widget; on each 1-second Redis poll, check `EXISTS kpidash:fortune:pushed` — if present, GET and display it immediately overriding the rotation timer (U2, A2)
- [X] T054 [P] [US9] Add `fortune push <text>` command with `--duration SECONDS` option (default `300`) to `clients/kpidash-client/kpidash_client/cli.py`: calls `redis_client.push_fortune(text, duration)` which does `SET kpidash:fortune:pushed '{"text":"...","source":"pushed","pushed_by":"{hostname}"}' EX {duration}`; document `--duration` in `README.md` (A2)

**Checkpoint**: US9 complete — fortune widget shows local content, accepts pushed fortunes

---

## Phase 12: Polish & Cross-Cutting Concerns

**Purpose**: Layout finalization, docs update, standards gate, smoke test

- [X] T055 Compose the full LVGL screen layout in `src/ui.c`: position client card grid (top area, max `MAX_CLIENTS` slots), Activities widget (right column or bottom-left), Repo Status widget (adjacent to Activities), Fortune widget (bottom strip), Status bar (bottom edge, overlaid or reserved row, hidden when empty); FR-008 enforcement checklist — after composing layout, verify each widget (client_card, activities, repo_status, fortune, status_bar, redis error overlay) does NOT contain scroll containers, `lv_list`, tooltips, dropdowns, or any container with `LV_OBJ_FLAG_SCROLLABLE` set; fix any violations before proceeding to T060 (U3)
- [X] T056 [P] Add `src/config.c` support for `KPIDASH_PRIORITY_CLIENTS` env var: comma-separated hostnames; dashboard places priority clients first in the card grid and never evicts them for non-priority clients; eviction logic: when `MAX_CLIENTS` reached, remove the non-priority client with the oldest `last_seen_ts`
- [X] T057 [P] Update `docs/ARCHITECTURE.md`: replace UDP/POC architecture description with Redis-based multi-component architecture; update component diagram and technology table
- [X] T058 [P] Update `docs/CLIENT-PROTOCOL.md`: replace UDP protocol spec with the Redis schema from `contracts/redis-schema.md` as the canonical client protocol reference
- [X] T059 [P] Update `docs/HANDOFF-CROSSCOMPILE.md`: add `libhiredis-dev:arm64` to the dependency list; update build command to reflect new `src/` structure
- [X] T060 Run full C pre-commit gate: `clang-format --dry-run --Werror src/**/*.c src/**/*.h`; `cppcheck --enable=warning,style,performance --error-exitcode=1 src/`; `cmake -B build && cmake --build build --parallel`; fix all warnings and errors
- [X] T061 [P] Run full Python pre-commit gate for both packages: `ruff format --check . && ruff check . && ty check && pytest -q`; fix all issues
- [X] T062a [P] Write `scripts/load_test.py`: spawn 8 concurrent threads each writing health (TTL=`HEALTH_TTL_S`) and telemetry to unique synthetic hostnames at intervals defined in `contracts/redis-schema.md`; run on Pi 5 alongside `kpidash`; pass criterion: dashboard renders all 8 cards with no visible freeze or flicker (SC-006); document pass/fail outcome alongside smoke test notes (C3)
- [X] T062 On-device smoke test and soak test on Pi 5: **(a) Functional smoke test**: deploy cross-compiled `kpidash`; start at least one Linux client daemon; verify client card appears, health indicator works, telemetry updates, activity CLI round-trip, fortune display, status bar acknowledge; run `scripts/load_test.py` (T062a) and confirm SC-006; **(b) 72-hour soak test (SC-008)**: leave `kpidash` running overnight with at least one client active; collect heap snapshot at start and end: `cat /proc/$(pgrep kpidash)/status | grep -E 'VmRSS|VmSize|VmPeak'`; if growth is suspected, run `valgrind --tool=massif ./kpidash` on a native (non-cross) build to isolate the leak; pass criterion: `VmRSS` growth < 10 MB over 72 hours, no visible degradation; document all results in `docs/SMOKE-TEST-RESULTS.md` (C4)

---

## Dependencies (User Story Completion Order)

```
Phase 1 (Setup) ──────────────┐
Phase 2 (Foundation) ─────────┤
                               ▼
                        US1 (Health cards)     ← Phase 3
                               │
                        US2 (Telemetry)        ← Phase 4 (dashboard + daemon)
                               │
                     ┌─────────┴──────────┐
                     ▼                    ▼
              US3 (Activities)     US4 (Repos)    ← Phases 5, 6 (parallel)
                     │
              US5 (Linux Client)               ← Phase 7 (wraps US2+US3+US4)
                     │
              US6 (Windows Client)             ← Phase 8 (parallel with US7)
              US7 (MCP Server)                 ← Phase 9 (parallel with US6)
                     │
              US8 (Status/Logs)                ← Phase 10
              US9 (Fortune)                    ← Phase 11 (parallel with US8)
                     │
               Polish                          ← Phase 12
```

**Parallelization per phase** (tasks marked [P] within each phase can run concurrently):
- **Phase 2**: T010a/T010b (C unit tests, [P] alongside T011–T014 Python tasks)
- **Phase 3**: T015 (widget) ∥ T016 (main.c) ∥ T017/T017a (ui + redis error overlay) ∥ T018 (redis health)
- **Phase 4**: T019/T020 (dashboard) ∥ T021/T022/T023 (Python collectors) ∥ T025 (tests)
- **Phase 5**: T026/T027 (dashboard) ∥ T028/T029 (Python CLI/tests)
- **Phase 6**: T030/T031 (dashboard) ∥ T032/T034 (Python scanner/tests)
- **Phases 8 & 9**: US6 and US7 development fully parallel
- **Phases 10 & 11**: US8 and US9 dashboard widgets fully parallel

## Implementation Strategy

**Suggested MVP scope** (delivers US1 alone = baseline working dashboard):
1. Complete Phases 1–3 first for a deployable health-monitoring dashboard
2. Add Phase 4 (telemetry) for the primary user value
3. Add Phase 5 (activities) for the first global widget
4. Continue in priority order

**Total task count**: 66 tasks
- Phase 1 (Setup): 7 tasks
- Phase 2 (Foundation): 9 tasks (includes T010a, T010b C unit tests)
- Phase 3 (US1): 5 tasks (includes T017a Redis unavailable UI)
- Phase 4 (US2): 7 tasks
- Phase 5 (US3): 4 tasks
- Phase 6 (US4): 5 tasks
- Phase 7 (US5): 4 tasks
- Phase 8 (US6): 4 tasks
- Phase 9 (US7): 5 tasks
- Phase 10 (US8): 4 tasks
- Phase 11 (US9): 3 tasks
- Phase 12 (Polish): 9 tasks (includes T062a load test)
