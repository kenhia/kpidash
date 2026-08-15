# Sprint 009 — kpidash backlog sweep

**korg proposal**: [korg:366](http://kubsdb:5674) — *kpidash backlog sweep — deploy, widget cleanup, °glyph + Apt-Temps card, dev-graph eviction, spec links*
**Branch**: `009-backlog-clear`
**Status**: #119 shipped; remaining items deferred

> **Ship note (2026-07-10):** this branch/PR ships **#119 only** (deploy
> hardening + version embedding + rpi53 cleanup). The other five items —
> #365, #363, #364, #250, #24 — were **not** started and remain open for a
> follow-up sprint (they're a coherent UI + docs chunk). The on-screen
> version *display* piece of #119 is also deferred into that layout pass;
> the version is embedded and published to `kpidash:system:version` today.

Catch-all sprint to clear most of the kpidash backlog. Six work items, done
roughly in the order below. `#365`, `#364`, and `#250` all touch
[src/ui.c](../../src/ui.c) / layout — coordinate them in one layout pass.

Excludes `#22` and `#23` (the Activities + Repo Status widgets are being turned
off; both parked `deferred` / `widgets-off`).

---

## Order of work

### 1. #119 — Solidify deploy: version visibility, safe deploy script, rpi53 cleanup  _(task / M)_

Do this **first** — a trustworthy deploy + a visible "what's running" signal
makes verifying everything below on the Pi reliable.

- **Clean rpi53** of stale artifacts: the root-owned `~/kpidash.new` trap (a
  naive `scp … ~/kpidash.new && mv` silently ships an *old* binary), old
  backups (`~/kpidash.bak-pre008`), and the stale `~/src/kpidash` checkout
  (was on `002-exploration-sprint` @ Apr-4).
- **Safe idempotent deploy script** in [scripts/](../../scripts/): cross-build →
  `scp` to `/tmp` → `sudo install -m755 -o ken -g ken /tmp/… ~/kpidash` →
  restart `kpidash.service` → post-restart health check. Must not use the
  `~/kpidash.new` path. Consider a `--host` override. (Supersedes the fragile
  `deploy` target in [CMakeLists.txt](../../CMakeLists.txt).)
- **Surface the deployed version** on the dashboard: verify
  `kpidash:system:version` / `KPIDASH_VERSION` reflect the actual build (embed
  `git describe --always --dirty` + build date at compile time) and show it
  somewhere visible (status bar / system widget).
- **Document** build → deploy → verify → rollback in
  [docs/HANDOFF-CROSSCOMPILE.md](../../docs/HANDOFF-CROSSCOMPILE.md) (or a new
  `docs/DEPLOY.md`).

**Acceptance**: one documented command builds+deploys+verifies from the dev
host; the dashboard shows the running version; rpi53 has no
stale/misleading artifacts; the deploy path can't ship an old binary.

### 2. #365 — Disable Activities and Repo Status widgets  _(task / S)_

- Hide/disable both widgets in [src/ui.c](../../src/ui.c) via a clean
  compile-time or config toggle (prefer over deleting code, so they can be
  re-enabled). Reflow the grid so no gap remains.
- No client change required. Frees footer/card space for `#364`.
- Supersedes `#22` (Repo Status flash) and `#23` (Activities project label)
  for now — both remain open, tagged `deferred`.

### 3. #363 — Add the `°` glyph  _(task / S)_

- Regenerate the font(s) via [fonts/generate.sh](../../fonts/generate.sh) to
  include U+00B0 DEGREE SIGN. Needed by `#364` (renders `75.6°F`).

### 4. #364 — Apt-Temps per-zone card  _(feature / S)_  — depends on #363

New card (same size/area as the services card), one per zone:

```
   Living
  --------
   75.6°F
     58%
```

Temp color: Blue `<65.0` · Green `65.0–75.0` · Orange `75.1–79.9` · Red `>79.9`.

**Redis contract (defined, TTL-less; dashboard owns freshness via `ts`)**:

```
Key:   kpidash:apttemps:<zone>     # <zone> = lowercase slug (living, kitchen, bedroom)
Type:  STRING (JSON)
Value: {"zone":"Living","temp_f":75.6,"humidity_pct":58,"ts":1783272200.0}
```

Dashboard SCANs `kpidash:apttemps:*` and renders one card per key (sorted by
zone); dim/gray if `(now - ts) > 300s` (threshold tunable). **kpidash only
consumes the keys** — the publisher is handed off to the separate **apt-temps**
project (schema doc: `ken@kai:~/src/homelab/apt-temps/.scratch/kpidash-apttemps-schema.md`).

### 5. #250 — Dev-graph host-series management  _(task)_

In-memory `g_graph_hosts[]` ([src/registry.c](../../src/registry.c),
`GRAPH_HOST_MAX=8`) never evicts, so stale/`"(legacy)"` series linger until a
dashboard restart. Preferred fix:

- **Auto-evict** a series once stale for a grace period (e.g. > 5 min) — a
  compaction pass in the poll loop ([src/redis.c](../../src/redis.c)) with safe
  reindexing of the parallel widget handles in [src/ui.c](../../src/ui.c).
- Optional: an evict command key (`kpidash:cmd:graph:evict`) and/or publishing
  the provisioned set for a `kpidash-client graph list/clear` CLI.

### 6. #24 — Fix spec markdown links for GitHub  _(task / S)_

- Rewrite `specs/**/*.md` links to repo-root files (e.g. `lv_conf.h`,
  `CMakeLists.txt`) to `../../…` so they render on GitHub, not just VS Code.
  (This file already follows that convention.)

---

## Notes

- Multiple `ui.c` / layout touchpoints (`#365`, `#364`, `#250`) — one
  coordinated layout pass.
- `#364` blocked on `#363` (glyph) — sequence accordingly.
- Close-out: [.korg-sprint-proposal](../../.korg-sprint-proposal) holds
  `korg:366` for `sprint-ship` to mark the proposal done on merge.
