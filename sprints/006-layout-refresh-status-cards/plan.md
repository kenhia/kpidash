# Implementation Plan: Layout Refresh, Service Status Cards, and Graph Polish

**Branch**: `006-layout-refresh-status-cards` | **Date**: 2026-05-23 | **Spec**: [spec.md](spec.md)
**Input**: Feature specification from `/specs/006-layout-refresh-status-cards/spec.md`

## Summary

Sprint 006 reshapes the visible dashboard composition: Row 1 stays Client Cards, rows 2-3 become a single 12-cell priority pool (Graph → Activities → Repo Status → Fortune), and the 276 px footer is reclaimed as a strip of small Service Status Cards. The Graph widget loses its on/off command, becomes always-on with one instance per reporting host, gains a "NO NEW DATA" overlay when its host stops publishing for ≥ 30 s, and renders GPU traces 2–3× thicker than the others. Fortune is relocated out of the footer into a 2×1 cell with a larger fixed font.

A new `kpidash-mockup` native-only build target lets developers iterate on footer card visuals (sizing, borders, icons, fonts) without deploying to the Pi. Service state is read from a new `kpidash:services:<name>` Redis namespace consistent with the existing `kpidash:system:*` / `kpidash:client:*` conventions; freshness (60 s) is computed by the dashboard against the payload `ts`, not via Redis TTLs. Icons are referenced by integer index into a dashboard-owned registry backed by a curated nerd-font subset compiled in via the existing `fonts/generate.sh` / `lv_font_conv` pipeline. The existing `clients/kpidash-client` Python CLI gains a new `service-status` subcommand (no separate binary).

## Technical Context

**Language/Version**: C11 (dashboard binary), Python ≥ 3.11 (client subcommand)  
**Primary Dependencies**: LVGL 9.2.2 (vendored at `lib/lvgl/`), hiredis (Redis client), cJSON (payload parse), libdrm + libpng (Pi DRM/KMS backend), X11 (native dev backend). Client: `click`, `redis`.  
**Storage**: Redis 7.x — existing `kpidash:*` namespace; this sprint adds `kpidash:services:<name>` (string, JSON value; no TTL — dashboard owns freshness) and extends the existing `kpidash:client:<host>:dev_telemetry` payload with a `host` field.  
**Testing**: Existing CMake/CTest harness under `tests/` (currently `test_config`, `test_layout`, `test_redis_json`, `test_widget_leak`). New unit tests for service-state parsing, freshness/colour mapping, icon-registry lookup, multi-host graph routing, and layout placement of the rows 2-3 priority pool. Python client: `pytest` + `ruff` + `ty` per constitution.  
**Target Platform**: Linux on Raspberry Pi 5 (aarch64, DRM/KMS, 3840×2160 @ headless); native x86_64 Linux for development (LVGL X11 backend). Cross-compile via `cmake/aarch64-toolchain.cmake` against `~/pi5-sysroot`.  
**Project Type**: Embedded-style desktop application (single C binary) plus an out-of-process Python CLI client. New native-only `kpidash-mockup` executable target sits alongside `kpidash`.  
**Performance Goals**: 1 s UI refresh cadence (unchanged); ≤ 35 s "NO NEW DATA" detection latency (30 s threshold + ≤ 5 s poll slack); Service Status Card border updates visible within 2 s of payload publish; sustained operation with no leak regression versus sprint 005 baseline (≈ 50 KB/h RSS drift previously eliminated — must stay flat).  
**Constraints**: Fixed 6×3 grid + 276 px footer (`src/layout.h`, do not change geometry); Pi 5 RAM budget unchanged from sprint 005 (a few hundred KB of additional font data is acceptable); no user-facing controls of any kind (FR-010 reaffirms the no-toggle stance); cross-compile path MUST NOT build the mockup target.  
**Scale/Scope**: Up to ~18 Service Status Cards visible concurrently in the footer (final sizing during mockup work); up to a small handful of Graph instances in rows 2-3 (one per reporting host — practically 1–4); curated icon registry on the order of ~32 entries for the foreseeable future.  

## Constitution Check

*GATE: Must pass before Phase 0 research. Re-check after Phase 1 design.*

Evaluated against `.specify/memory/constitution.md` v1.2.0.

| Principle | Status | Notes |
|-----------|--------|-------|
| I. Spec-Driven Development | ✅ Pass | Spec `specs/006-layout-refresh-status-cards/spec.md` exists, has a `## Clarifications` block (Session 2026-05-23) resolving FR-022a and FR-015a, and contains no `[NEEDS CLARIFICATION]` markers. All work in this plan is bounded by that spec. |
| II. Documentation in docs/ | ✅ Pass | `docs/CLIENT-PROTOCOL.md` will receive a new section for the `kpidash:services:<name>` key and a `host`-field extension to dev-telemetry samples during implementation (tracked, not a plan-time deliverable). Architecture-level docs do not require changes for this sprint. |
| III. Test-Driven Development | ✅ Pass | Logic-heavy additions (service-state parser, freshness/colour mapping, icon-registry lookup, host-keyed graph router, rows-2-3 priority placement) are testable without LVGL and will follow Red-Green-Refactor. The "NO NEW DATA" overlay, Fortune font choice, GPU trace thickness, and mockup visuals are GUI-layer and validated by on-device / native-window smoke tests per the principle's GUI exemption. |
| IV. Code Standards Gate | ✅ Pass | New widgets land under `src/widgets/` and are already covered by the constitution's clang-format glob (amendment 1.2.0). Pre-commit: clang-format, cppcheck, native + cross build, ctest. Python client subcommand additions covered by `ruff format/check`, `ty check`, `pytest`. |
| V. Simplicity & Intentional Design | ✅ Pass | No new abstractions beyond what the spec requires: one new widget class (Service Status Card), one new registry (icons), one new Redis key family, one new build target, one new client subcommand. The icon registry is a flat integer→codepoint table (not a plugin system); the freshness check is a single wall-clock comparison (no scheduler); the mockup target reuses the existing X11 build path. |

**Initial Constitution Check: PASS — no violations to record in Complexity Tracking.**

## Project Structure

### Documentation (this feature)

```text
specs/006-layout-refresh-status-cards/
├── plan.md              # This file
├── spec.md              # Feature spec (already present)
├── research.md          # Phase 0 output
├── data-model.md        # Phase 1 output
├── quickstart.md        # Phase 1 output
├── contracts/           # Phase 1 output
│   ├── redis-services.md
│   ├── redis-dev-telemetry-host.md
│   └── icon-registry.md
└── tasks.md             # Phase 2 output (NOT created by /speckit.plan)
```

### Source Code (repository root)

Single-project C binary plus an existing Python client package. Sprint 006 adds files in the already-established locations; no new top-level directories are required.

```text
src/
├── layout.h                       # unchanged (6×3 grid + 276 px footer)
├── ui.c / ui.h                    # extended: rows 2-3 priority pool placement;
│                                  #            footer now hosts Service Status Cards
├── redis.c / redis.h              # extended: services scan + parse;
│                                  #            dev_telemetry payload now carries `host`;
│                                  #            remove kpidash:cmd:graph dependency
├── registry.c / registry.h        # extended: per-service state, per-host graph series
├── icons.c / icons.h              # NEW: integer-index → nerd-font codepoint registry
├── mockup_main.c                  # NEW: native-only entry for kpidash-mockup target
└── widgets/
    ├── service_card.c / .h        # NEW: Service Status Card widget
    ├── dev_graph.c / .h           # extended: per-host instance, "NO NEW DATA" overlay,
    │                              #            thicker GPU traces
    └── fortune.c / .h             # extended: 2×1 cell, larger fixed font (and optional
                                   #            stretch dynamic sizing per FR-009)

fonts/
├── generate.sh                    # extended: emit a nerd-font icon font (size ~48–64 px)
├── lv_font_icons_<sz>.c           # NEW: generated icon font (committed, like the others)
└── ttf/SymbolsNerdFont-Regular.ttf# already vendored

tests/
├── test_service_card.c            # NEW: parse, freshness, colour mapping, malformed payload
├── test_icon_registry.c           # NEW: index lookup, unknown index fallback
├── test_graph_router.c            # NEW: per-host dispatch + stale detection (≥30s)
└── test_layout_pool.c             # NEW: rows-2-3 priority placement + overflow drop

clients/kpidash-client/kpidash_client/
└── cli.py                         # extended: new `service-status` subcommand

CMakeLists.txt                     # extended: kpidash-mockup executable
                                   #            (guarded so Pi cross-compile excludes it)

docs/
└── CLIENT-PROTOCOL.md             # extended during implementation: services namespace,
                                   #            dev-telemetry `host` field
```

**Structure Decision**: Single-project layout (matching the existing repository). New widget code goes under `src/widgets/`. The mockup binary is a second `add_executable` in the root `CMakeLists.txt`, guarded so that `CMAKE_CROSSCOMPILING` (or `TESTS_ONLY=ON`) skips it. The icon registry is a small standalone C unit (`src/icons.{c,h}`) rather than living inside `ui.c`, so it is unit-testable without LVGL.

## Complexity Tracking

> No constitution violations recorded.

| Violation | Why Needed | Simpler Alternative Rejected Because |
|-----------|------------|-------------------------------------|
| _(none)_ | _(n/a)_ | _(n/a)_ |

---

## Phase 0 — Research

See [research.md](research.md). The four open technical questions enumerated in the workflow prompt are resolved:

1. Nerd-font glyph subset selection and `lv_font_conv` invocation for icon glyphs.
2. LVGL approach for the "NO NEW DATA" overlay on a chart widget.
3. Native X11 mockup target structure (separate `add_executable` vs build flag).
4. Service Status Card Redis key naming convention consistent with the existing `kpidash:system:*` namespace.

All Phase 0 items have a Decision / Rationale / Alternatives entry. No item deferred to `/speckit.tasks`.

## Phase 1 — Design & Contracts

Generated artifacts:

- [data-model.md](data-model.md) — Service status payload, Graph sample (with `host`), Icon registry entry, Service Card state machine (including FR-022a "ignore malformed payload" and FR-015a "no eviction"), Graph host series, Layout pool.
- [contracts/redis-services.md](contracts/redis-services.md) — `kpidash:services:<name>` key contract.
- [contracts/redis-dev-telemetry-host.md](contracts/redis-dev-telemetry-host.md) — `host` field extension to dev-telemetry samples.
- [contracts/icon-registry.md](contracts/icon-registry.md) — Dashboard-owned integer→glyph mapping format and lookup semantics.
- [quickstart.md](quickstart.md) — Build / test / deploy steps including the new `kpidash-mockup` target and configuring `kpidash-client` on `kai` for graph reporting.

Agent context regenerated via `.specify/scripts/bash/update-agent-context.sh copilot` (see post-design re-evaluation below).

### Post-Design Re-Evaluation (Constitution Check)

Re-checked against `.specify/memory/constitution.md` v1.2.0 after Phase 1 artifacts were produced.

| Principle | Status | Notes |
|-----------|--------|-------|
| I. SDD | ✅ Pass | All Phase 1 outputs trace back to FR-IDs in the spec. |
| II. Docs | ✅ Pass | Contract docs live under the spec directory; `docs/CLIENT-PROTOCOL.md` update is scheduled for the implementation phase. |
| III. TDD | ✅ Pass | All new logic units have a corresponding `tests/test_*.c` entry in the structure above. |
| IV. Code Standards Gate | ✅ Pass | No new file locations escape the existing clang-format / cppcheck globs. |
| V. Simplicity | ✅ Pass | Design adds the minimum surface required by the spec — no speculative extension points. |

**Post-Design Constitution Check: PASS.** No new violations introduced by the design.

---

## Stop Point

Per the speckit.plan workflow, execution stops after Phase 2 planning. `tasks.md` is intentionally NOT generated by this command — it is the output of `/speckit.tasks`.
