# 014 — kprojects harness

korg #1263 (chore, M) · proposal #1266 · batch 4 of the kprojects rollout (#737)

## Goal

Migrate kpidash off Spec-Kit onto the kprojects minimal harness: collapse the
twelve spec directories into `sprints/`, remove the Spec-Kit machinery, and give
the repo a `just check` that does something real.

## What shipped

- **Harness applied** — `kproject-install --agent both`, stack `other`
  (detected). `sprints/{planning,review}`, `docs/`, `.scratch/`, `.env` in
  `.gitignore`, managed block in both agent files.
- **Twelve spec directories collapsed into `sprints/`**, numbering preserved
  (the numbers match the repo's GitHub branch names, so they are history).
  Directories with several docs stayed directories (001–007); the five that held
  a single file became `sprints/NNN-name.md` (008, 009, 011–013). 010 never
  existed.
- **Spec-Kit machinery removed** — `.specify/` (templates, scripts,
  constitution), `.github/prompts/`, `.github/skills/speckit-*`,
  `.github/agents/`.
- **A real gate.** `just check` configures with `-DTESTS_ONLY=ON`, builds, runs
  `ctest`. 7 tests, passing from a clean tree.
- **Both agent files** carry the managed block at the top and an equivalent
  `## Project` section — what this is, how to build/test/deploy, what to read
  first, and the gotchas that are not derivable from the code.
- Doc cross-references repointed from `specs/…` to `sprints/…`; README's
  structure block and test section updated.

## Decisions

**`other` was the right stack, and it was not a miss.** The root is
`CMakeLists.txt`/`src/`/`lib/`/`cmake/`; the only `pyproject.toml` is three
levels down in `clients/kpidash-client/`, a client package rather than the
project. `--stack python` would have stamped a Python tooling stanza onto a C
project. **The stack call was still pending when this ran** — korg #1259 and
#1260 were both open — so this took the fallback the work item prescribed:
`other` plus a hand-written gate. If a `cmake` stack lands later, re-apply with
an explicit `--stack`.

**The gate came from the README, not from CI.** There is no CI in this repo, so
#1259 and #1260 both assumed a gate would have to be invented. It did not:
`CMakeLists.txt` already carries a `TESTS_ONLY` option documented as "use this
for native (x86_64) CI", and the README already documents the exact three
commands. `just check` is that, verbatim. **This is worth feeding back to #1260**
— "no CI to mirror" was true, but "no source of truth to copy" was not.

**`clang-format` and `cppcheck` stayed out of the gate.** The retired constitution
claimed both as pre-commit checks; neither is installed on kai, so wiring them in
would have seeded a gate that fails on a clean checkout — the thing the rollout
has consistently refused to do. Recorded in the roadmap's Later section instead.

**The peer-programming stance was dropped, not folded.** `.github/copilot-
instructions.md` held a generic pair-programming stance (show-before-you-proceed,
pause-after-each-step) and no project facts. Commit d0ebc5b retired the matching
`pair-on-a-change` skill on 2026-07-27 under korg #727 and archived it elsewhere,
but left this file behind. Confirmed with Ken during the sprint: treat it as a
leftover of the retired experiment. Recoverable from git if that turns out wrong.

**The real project content was in `.github/agents/copilot-instructions.md`**, not
the root copilot file — Spec-Kit's auto-generated "Development Guidelines", 118
lines of stack, commands, key constraints and Redis conventions. That, plus the
constitution's durable parts, is what became `## Project`. The work item expected
to fold the root file; it was the wrong file.

## Follow-ups

- **korg #1260** — the "no gate derivable" premise does not hold for kpidash (see
  above). Whether C/CMake earns a fifth stack is now a question about one repo
  that already has a working hand-written gate.
- **korg #1259** — kpidash is off the list: it has a `check` recipe now.
- `just --list` shows a wrong description for `publish` (just takes the *last*
  comment line before a recipe). Pre-existing, left alone.
- `scripts/deploy.sh` builds into `build-pi/` while the repo carries `build-pi5/`.
  Pre-existing drift, not touched here.
- `tests/test_widget_leak.c` only builds when LVGL is present, so the gate never
  runs it — the one test guarding the bug class sprint 005 fixed. In the roadmap.
