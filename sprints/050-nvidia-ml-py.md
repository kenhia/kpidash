# 050 — kpidash-client 2.1.1: `nvidia-ml-py` replaces `pynvml`

Proposal korg:5076 covers WI 4608, a slice of the low-hanging-fruit program
korg:5082 (2026-11-26). It ran as a karc leg (kpidash-7247af) on kai under an
overseer, on branch `050-nvidia-ml-py`.

## Goal

Stop the deprecation notice every Linux host logs when kpidash-client starts,
by depending on `nvidia-ml-py` directly instead of the deprecated `pynvml`
distribution. Then cut a patch release and roll it out.

## Premise check

- **Holds.** The locked `pynvml 13.0.1` is a shim that depends on
  `nvidia-ml-py 13.595.45` and prints, at import,
  `FutureWarning: The pynvml package is deprecated. Please install
  nvidia-ml-py instead.` That was reproduced in kai's venv. Over the last 30
  days the client journals hold that notice on every awake Linux host: kai 7,
  kubs0 6, kubsdb 9, gratch 8, rpi53 8. The probe ran from kai, the host that
  does the rollout.
- **The fleet is on 2.0.0, not 2.1.0.** 2.1.0 (sprint 048,
  `service-status --title/--line/--span`) was published to the store but never
  rolled out, because the k-homelab floor stayed at 2.0.0. So this rollout also
  carries 048's CLI additions. They are additive, and the daemon path is
  unchanged since 2.0.0.
- **kubsdb** is not a source install. Like kai, kubs0 and gratch, it runs
  `uv tool` from the package store, at 2.0.0 (sprint 036's table and
  `uv tool list` today). So "its clone is gone" does not affect the rollout.
- **kimac** was asleep (`ssh: connect … Connection timed out` from kai,
  10:15 PST).
- kpidash is not listed in the cross-project-planning routing table.

## What changed

- `pyproject.toml`: `pynvml>=11` → `nvidia-ml-py>=12`; version 2.1.0 → 2.1.1
  (patch, per the overseer's call). `uv lock` removed `pynvml` and kept
  `nvidia-ml-py 13.595.45`. The import name is still `pynvml` (it is
  `nvidia-ml-py`'s module), so `telemetry/gpu.py` is unchanged apart from its
  docstring.
- `tests/test_dependencies.py` (new, written first and seen failing): the
  declared dependencies include `nvidia-ml-py` and not `pynvml`.
- Doc mentions: `docs/ARCHITECTURE.md`, the client README, CLAUDE.md.

## Verified on kai

- `import pynvml` in the synced venv prints no warning.
- `collect_gpu()` with `-W error::FutureWarning` returns
  `{'name': 'NVIDIA GeForce RTX 5090', 'vram_used_mb': 31047,
  'vram_total_mb': 32607, 'compute_pct': 0.0}`, the same fields as before.
- `just check` rc=0: ctest 25/25, client pytest 174 passed, ruff clean, shell
  tests green.

## Repaired in passing

- `docs/ARCHITECTURE.md`'s tech table still listed GitPython as a client
  dependency, but it was removed in 2.0.0 (sprint 036). It was dropped in the
  same edit. Gate: n/a (doc only).
- `sprints/planning/roadmap.md` "Now" stopped at 043. 044–049 were added, one
  line each, from their records.

## Cross-repo (relayed, not edited)

k-homelab is held by another leg. The overseer relays this line:
`manifests/common.yml`, key `kpidash_min_version`, `"2.0.0"` → `"2.1.1"`, with
a comment paragraph below the 2.0.0 one. The paragraph is in the wrap-up
handoff.

## Rollout plan (the ship turn, after clearance)

1. Check the apt ring is not mid-run.
2. `just publish` (2.1.1 to the store on kubsdb).
3. One host at a time: kai, kubs0, kubsdb, gratch: `uv tool upgrade
   kpidash-client`, restart the system unit. rpi53 by sprint 036's route
   (rsync of kai's checkout, `uv sync`, restart). kimac if awake.
4. After each, check that the journal shows no pynvml notice since the
   restart, the unit is `active` with `NRestarts=0`, and health and telemetry
   are fresh on rpi53. On kai and kubs0, check the GPU fields in
   `kpidash:client:<host>:telemetry`.
