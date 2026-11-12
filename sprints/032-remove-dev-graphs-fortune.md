# 032 — Remove the dev graphs, Fortune, Activities and Repo Status

Proposal korg:4562 covers WI 4561. It ran as a karc leg (kpidash-85c117) on
kai under an overseer.

## Goal

The host columns (sprints 029-031) now show everything the per-host dev graphs
showed, so the graphs go. Fortune goes with them: Ken wants something graphical
there later, not Fortune. Activities and Repo Status have been compiled out
since sprint 012 (WI 365), and they go in the same change. The code is
removed, not toggled off, and the panel settles into:

- **Row 0:** host columns, unchanged.
- **Row 1:** the 10G card in columns 0-1, the NAS card in columns 2-3, and
  columns 4-5 empty.
- **Row 2:** empty.
- **Footer:** service strip and status bar, unchanged.

## Premise check

| claim | verdict |
|---|---|
| The four widgets exist as `src/widgets/{dev_graph,fortune,activities,repo_status}` plus `src/fortune.c` | **holds** |
| `hosthist` may share a feed with the dev graphs | **holds in a different place.** `hosthist` is fed from `telemetry`, not `dev_telemetry`, so it is untouched. The `graph_host` series, the dev graphs' own feed, **does** have a second reader: the NAS card's CPU/RAM chart (`nas_card.c` `update_chart`). |
| The pool placer may be more machinery than two fixed cards need | **holds.** Replaced it (below). |

## Decisions

- **Retrieval tag first.** `last-with-dev-graphs-fortune` is an annotated tag
  on `fab438f` (the `origin/main` the branch started from), pushed to origin
  before any deletion. `docs/ARCHITECTURE.md` gained a "Removed widgets"
  section with one row per widget: what it showed, and the paths to check out
  at the tag.
- **The `graph_host` series stays, trimmed to what the NAS card reads.** It
  keeps `graph_host_find_or_create`/`touch` and the telemetry, and loses the
  widget slot, the `stale` flag, `is_stale`, `snapshot`, `remove` and the
  eviction pass in `ui.c`. Eviction existed only so dead hosts' graph widgets
  would not linger. With one host polled, the table cannot fill.
- **`redis_poll` GETs only the NAS host's `dev_telemetry`** (`KPIDASH_NAS_HOST`)
  instead of one GET per client every second. Other hosts' keys keep a reader
  elsewhere: kdeskdash parses `dev_telemetry` (its `src/dev_telemetry.c`), so
  the producer is not orphaned.
- **The pool placer is gone.** Two cards with fixed slots need no
  priority-sorted 12-cell packer. `layout_pool_place`, `widget_kind_t`,
  `widget_request_t`, `WIDGET_CELLS_*`, `LAYOUT_POOL_CAPACITY_CELLS` and
  `test_layout_pool` went. Both cards are created at fixed positions in
  `ui_init`. The NAS card's slot stays reserved while it is hidden, so the 10G
  card never moves.
- **No dead toggles.** `KPIDASH_WIDGET_{FORTUNE,ACTIVITIES,REPO_STATUS,NETCARD}`
  went, and so did `KPIDASH_ACTIVITY_MAX` (`config`), the
  `kpidash:cmd:graph{,:host}` and `kpidash:cmd:fortune_dev` dev commands, and
  their parsers and tests. The dev overlays (`dev_grid`, `dev_textsize`) stay,
  as the brief said.
- **The NAS card's HIDDEN flag changes only on a transition.** In LVGL 9.2,
  `lv_obj_remove_flag(…, HIDDEN)` invalidates the whole object even when it is
  already visible. The pool code called it every second on both row-1 cards,
  which forced a full repaint of 2×1 units each second. The 10G card is never
  hidden now.
- **The contract doc keeps the schemas.** CLIENT-PROTOCOL §4-§6 (activities,
  repos, Fortune) are marked *unread since sprint 032* rather than deleted,
  because producers still write them. §9.3/§9.4 (the dev commands, which have
  no producer) collapse to a one-paragraph "removed" note, and §9.5 keeps its
  number.

## What shipped

- Deleted: `src/fortune.{c,h}` and
  `src/widgets/{dev_graph,fortune,activities,repo_status}.{c,h}`.
  `tests/test_layout_pool.c` went too.
- `src/ui.c`: fixed row-1 placement replaces the pool block and the graph
  eviction.
- `src/redis.{c,h}`: no activities, repos or fortune reads, no graph or
  fortune_dev commands, and `dev_telemetry` for the NAS host only.
- `src/registry.{c,h}`: `activity_t`, `repo_entry_t`, the pool and the
  graph-only series fields are removed.
- `src/{protocol,config,main,ui}.h/.c`: the keys, constants, toggles and the
  `fortune_init` call are removed.
- Tests: `test_redis_json` loses the graph-command and repo-parse cases,
  `test_config` loses `activity_max`, `test_graph_router` keeps the four cases
  that still have a subject, and the `fortune_on_pushed` stubs went.
- Docs: README, ARCHITECTURE (layout, data flow, source tree, Removed
  widgets), CLIENT-PROTOCOL, CLAUDE.md, the Copilot mirror, a "historical in
  places" header on HANDOFF-CROSSCOMPILE, and the roadmap.
- Net diff: 40 files, +171 / −2549.
- Gates: `just check` is green (ctest 16/16, client pytest 180). So is
  `just check-warnings-full` (whole tree at `-O2 -Werror`, 0 warnings; ctest
  17/17 including `test_widget_leak`).

## Repaired in passing

- **The version string picked up the new tag.** `cmake/gen_version.cmake` ran
  `git describe --tags`, and the repo had never had a tag. So the first deploy
  self-reported `last-with-dev-graphs-fortune-1-gb476e0b` on rpidash's service
  card. It is now `--match v[0-9]*`, which gives back the bare hash
  (`06f59a4`). A real `v1.2.3` release tag would still name a build. Proved by
  the redeploy's read-back.

## Producers left publishing to nobody

Out of scope by the brief (no edits to other repos). The in-repo client is a
published fleet package, and pulling features from it needs a release and a
rollout, so those changes are filed, not made:

- `kpidash-client`: `write_repos` (repo scanning, `kpidash:repos:{host}`), and
  the `activity start|done` and `fortune push` CLI commands. `dev_telemetry`
  is *not* in this list, because kdeskdash still reads it.
- `clients/kpidash-mcp`: the activity MCP server. It was retired from the fleet
  in korg:2381, but its package still lives in this repo.
- Other repos: a fleet search (kaed `*:src`, plus a grep of kai's `~/src`) found
  no other writer or reader of `kpidash:fortune:*`, `kpidash:activities` or
  `kpidash:repos:*`, apart from documentation. kdashdata's
  `contracts/registry.md` still lists all three (and `kpidash:cmd:graph` and
  `fortune_dev`) under "kpidash-internal". That table is kdashdata's, so this
  sprint did not edit it; it is part of the filed item.

## Pre-ship deploy and the CPU number

The branch build was deployed to rpi53 from kai with `scripts/deploy.sh`,
ahead of the clearance, as sprint 030 did. The ship turn redeploys from the
merged `main`.

- `b476e0b` went out at 14:06 PST and self-reported
  `last-with-dev-graphs-fortune-1-gb476e0b` (see Repaired in passing).
  `06f59a4` went out at 14:06:59 PST and reports `06f59a4 (2026-11-12)`.
  The service is active, `NRestarts=0`, and the journal shows no warnings or
  errors. The aarch64 build and link printed no warnings
  (`.scratch/deploy-032.log`, grepped).
- **Captures** (`.scratch/live-032.png` right after the restart,
  `.scratch/live-032-full.png` at 10 min): row 0 is the host columns,
  unchanged. Row 1 has the 10G card in columns 0-1 and the NAS card in 2-3,
  with columns 4-5 empty. Row 2 is empty, and the footer is unchanged. The
  NAS card's activity chart fills from the NAS-only `dev_telemetry` poll, and
  the rpidash service card reads `06f59a4 (2026-11-12)`.
- **CPU, `top` on rpi53, % of one core, rings full:**

  | build | kpidash CPU | RSS |
  |---|---|---|
  | 030 tuned (030's figure) | 10.6 % | 69 MB |
  | 031 `df59d7c`, 2 h up, measured at the start of this sprint | 11.3-11.5 % | 101 MB |
  | **032 `06f59a4`, 623 s up, six 10 s samples** | **6.1-6.5 %** | **69 MB** |

  That is about 45 % less than 031. The likely causes are the per-second
  full-object invalidation of both row-1 cards (gone) and the per-host
  `dev_telemetry` and graph-command GETs (gone). This was not profiled
  separately.
- rpi53's own column shows regular top-core spikes over those ten minutes.
  They were my wait loop, run on rpi53 itself: `ps` every 10 s, then `top`.
  They are not the dashboard.
