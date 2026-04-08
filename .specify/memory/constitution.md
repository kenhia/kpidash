<!-- Sync Impact Report
Version change: N/A → 1.0.0 (initial ratification)
Added principles:
  - I. Spec-Driven Development (SDD)
  - II. Documentation in docs/
  - III. Test-Driven Development (TDD) — encouraged where appropriate for embedded/GUI C
  - IV. Code Standards Gate (C/CMake toolchain)
  - V. Simplicity & Intentional Design
Added sections:
  - Directory Structure
  - Pre-Commit Checks (C)
  - Development Workflow
  - Governance
Removed sections: none
Templates requiring updates:
  - .specify/templates/plan-template.md ✅ no changes needed
  - .specify/templates/spec-template.md ✅ no changes needed
  - .specify/templates/tasks-template.md ✅ no changes needed
Follow-up TODOs: none

Amendment 1.0.0 → 1.1.0 (2026-03-31, MINOR)
  - Principle II: fixed stale "UDP communication protocol" wording → "Redis communication schema" (I1)
  - Principle IV: added Python / pytest standards gate subsection for kpidash-client and kpidash-mcp (I2)
  - Directory Structure: added `clients/` entry
Templates requiring updates: none (no structural changes)

Amendment 1.1.0 → 1.2.0 (2026-04-05, MINOR)
  - Principle IV / Pre-Commit Checks: clang-format globs extended to include
    `src/widgets/*.c src/widgets/*.h` (D1 from speckit.analyze — src/widgets/
    added in Sprint 002 but constitution globs were not updated)
Templates requiring updates: none
-->

# kpidash Constitution

## Core Principles

### I. Spec-Driven Development (SDD)

All changes MUST be documented in `/specs/` before implementation begins.
Iteration-scoped changes live in their spec directory
(e.g., `/specs/001-feature/spec.md`). Ad-hoc changes that fall outside an
active spec MUST be added to the current spec or to
`/specs/supplemental-spec.md`.

- No code change without a corresponding spec entry.
- Specs define acceptance criteria before implementation starts.
- Spec updates are part of the definition of done for every iteration.

### II. Documentation in docs/

Architecture and design documents MUST live in the `docs/` directory. This
includes architecture overviews, protocol references, cross-compilation guides,
and hardware-specific integration notes.

- `docs/ARCHITECTURE.md` is the canonical reference for system design and
  component relationships.
- `docs/CLIENT-PROTOCOL.md` is the canonical reference for the dashboard Redis
  communication schema (key naming, value formats, TTLs, and access patterns).
- `docs/HANDOFF-CROSSCOMPILE.md` is the reference for cross-compilation
  workflow targeting the Raspberry Pi 5 (aarch64).
- There is no requirement to maintain architecture docs proactively; create
  them when they add value.
- If docs exist, they SHOULD be kept reasonably current during the polish
  phase of each spec.

### III. Test-Driven Development (TDD)

TDD is encouraged where appropriate. The Red-Green-Refactor cycle is the
preferred approach for business logic, protocol parsing, and stateful
registry operations. For hardware-dependent code (DRM/KMS display driver,
LVGL rendering pipeline, platform I/O), unit-level testing may not be
practical — integration testing via on-device smoke tests is acceptable in
those cases.

- Where unit tests are feasible, they SHOULD be written before or alongside
  the code they validate.
- Test coverage on testable modules SHOULD NOT decrease with new changes.
- UDP receive and JSON parse logic SHOULD be tested via loopback or message
  injection, not live hardware.
- GUI and display-layer code is exempt from the TDD requirement but MUST be
  manually validated on the target display before a feature is considered done.

### IV. Code Standards Gate

All code MUST pass the following checks before commit:

1. **Formatted** — source is auto-formatted with `clang-format` (project
   `.clang-format` config governs style).
2. **Linted** — static analysis passes with no errors (`cppcheck` or
   `clang-tidy`; warnings treated as errors in CI).
3. **Build** — clean CMake build succeeds for both native and cross-compiled
   (aarch64) targets; `-Wall -Wextra` is active and produces no warnings.
4. **Unit tests** — all applicable tests pass (`ctest`) where tests exist.

The CI variant of each check (strict/non-interactive) MUST pass clean before
any commit. This applies to both new and existing code — no broken windows.

See [Pre-Commit Checks](#pre-commit-checks) for specific tooling.

### V. Simplicity & Intentional Design

Every addition MUST justify its complexity. YAGNI applies strictly:

- Do not add features, abstractions, or configuration options for hypothetical
  future requirements.
- Prefer explicit over implicit behavior.
- Start with the simplest approach that meets the spec; refactor only when a
  measured need arises.
- Defensive coding at system boundaries only (UDP input, configuration parsing,
  filesystem access). Trust internal code and LVGL framework guarantees.
- Scope is defined by the active spec. Nothing beyond that without a spec.

## Directory Structure

The following directories MUST be used as specified. Create if they do not
exist.

| Directory | Purpose | Git tracked |
|-----------|---------|-------------|
| `.scratch-agent/` | Temporary workspace for agent use | No (`.gitignore`) |
| `.scratch/` | Temporary workspace for user use | No (`.gitignore`) |
| `cmake/` | CMake toolchain files (e.g., aarch64 cross-compile) | Yes |
| `docs/` | Project documentation (architecture, protocol, guides) | Yes |
| `lib/` | Third-party libraries as git submodules (LVGL) | Yes |
| `resources/` | Runtime assets (PNG images, fonts) | Yes |
| `specs/` | Iteration and supplemental specifications (SDD) | Yes |
| `src/` | C source and header files | Yes |
| `clients/` | Python client packages (`kpidash-client`, `kpidash-mcp`) | Yes |

- `README.md` lives at the project root, not in `docs/`.
- Spec directories use the pattern `specs/NNN-feature-name/`.
- `lv_conf.h` lives at the project root (required by LVGL's build system).

## Pre-Commit Checks

Primary language is C (C11). Build system is CMake.

### C / CMake

```bash
# Format (in-place)
clang-format -i src/*.c src/*.h src/widgets/*.c src/widgets/*.h

# Static analysis
cppcheck --enable=warning,style,performance --error-exitcode=1 src/

# Native build
cmake -B build && cmake --build build --parallel

# Tests (where applicable)
ctest --test-dir build -V

# CI variant (must pass clean before commit)
clang-format --dry-run --Werror src/*.c src/*.h src/widgets/*.c src/widgets/*.h
cppcheck --enable=warning,style,performance --error-exitcode=1 src/
cmake -B build && cmake --build build --parallel
ctest --test-dir build -V
```

> **Cross-compilation note**: When targeting aarch64 (Raspberry Pi 5), use the
> project toolchain file:
> `cmake -B build-pi -DCMAKE_TOOLCHAIN_FILE=cmake/aarch64-toolchain.cmake`

### Python / pytest (kpidash-client and kpidash-mcp)

Run from each Python package root (`clients/kpidash-client/`, `clients/kpidash-mcp/`).

```bash
# Format + lint + type check
ruff format --check .
ruff check .
ty check

# Tests
pytest -q

# CI variant (must pass clean before commit)
ruff format --check . && ruff check . && ty check && pytest -q
```

## Development Workflow

1. **Spec** — Define or update the spec (`/specs/`).
2. **Plan** — Create implementation plan from spec.
3. **Implement** — Follow TDD where practical; write tests first for
   logic-heavy modules; apply design-first for hardware/GUI layers.
4. **Check** — Run pre-commit checks (format, lint, build, test).
5. **Document** — Update `docs/` as needed.
6. **Review** — Verify constitution compliance before commit.

Ad-hoc changes follow the same workflow but reference
`/specs/supplemental-spec.md` instead of a feature spec.

## Governance

This constitution supersedes all other development practices for the kpidash
project. All code changes, reviews, and architectural decisions MUST verify
compliance with these principles.

**Amendment procedure**:
1. Propose the change with rationale.
2. Document the amendment in this file.
3. Update the version number per semantic versioning:
   - **MAJOR**: Principle removal or backward-incompatible redefinition.
   - **MINOR**: New principle or materially expanded guidance.
   - **PATCH**: Clarifications, wording, or typo fixes.
4. Update `LAST_AMENDED_DATE`.
5. Propagate changes to dependent templates and documentation.

**Compliance review**: Every commit MUST pass the Code Standards Gate
(Principle IV). Spec alignment (Principle I) is verified during iteration
polish.

**Version**: 1.1.0 | **Ratified**: 2026-03-30 | **Last Amended**: 2026-03-31
