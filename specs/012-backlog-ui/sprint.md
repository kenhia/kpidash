# Sprint 012 — kpidash backlog sweep (UI remainder of #366)

**korg proposal**: korg:366 — *kpidash backlog sweep*
**Branch**: `012-backlog-ui`
**Status**: active

Second slice of proposal #366. The first slice (#119 deploy hardening) shipped
on `009-backlog-clear`. This sprint takes the remaining items plus **#369**
(self service card — included this sprint; implemented as dashboard self-publish).

`#365`, `#364`, `#250`, `#369` all touch [src/ui.c](../../src/ui.c) / layout and
the footer service strip — do them as one coordinated pass. Excludes `#22`/`#23`
(widgets being turned off; parked `deferred`).

---

## Order of work

### 1. #363 — Add the `°` glyph  _(task / S)_
Regenerate the icon/text font(s) via [fonts/generate.sh](../../fonts/generate.sh)
to include U+00B0 DEGREE SIGN. **Active bug**: the running dashboard floods its
journal with `glyph dsc. not found for U+B0` because the live `apt-temps` card
already renders `°`. Unblocks #364.

### 2. #364 — Apt-Temps per-zone card  _(feature / S)_ — depends on #363
New card (services-card size/area), one per zone, from the Redis contract:

```
Key:   kpidash:apttemps:<zone>            # <zone> = lowercase slug
Value: {"zone":"Living","temp_f":75.6,"humidity_pct":58,"ts":...}
```

Render `zone / temp_f°F (1 dp) / humidity_pct%`; temp color Blue `<65` · Green
`65–75` · Orange `75.1–79.9` · Red `>79.9`; dim/gray if `(now-ts)>300s`.
Dashboard SCANs `kpidash:apttemps:*`. Publisher handed off to the apt-temps
project (schema at `ken@kai:~/src/homelab/apt-temps/.scratch/`).

### 3. #365 — Disable Activities and Repo Status widgets  _(task / S)_
Hide both via a clean compile/config toggle in [src/ui.c](../../src/ui.c);
reflow the grid so no gap remains. Frees footer/card space. Supersedes #22/#23
for now (both parked `deferred`).

### 4. #250 — Dev-graph host-series management  _(task)_
In-memory `g_graph_hosts[]` ([src/registry.c](../../src/registry.c)) never
evicts → stale/`"(legacy)"` series linger until restart. Preferred:
**auto-evict** a series once stale beyond a grace period (compaction pass in
[src/redis.c](../../src/redis.c) poll loop + safe reindex of the parallel
`ui.c` widget handles). Optional: evict command key / list CLI.

### 5. #369 — Self service card (rpidash)  _(feature / S)_
Dashboard **self-publishes** `kpidash:services:rpidash:rpi53` on startup/poll
(state `ok`, text incl. `KPIDASH_BUILD_VERSION` from #119, `ts=now`) so the
board shows its own liveness. Note: this intentionally has the dashboard *write*
one service key (small, documented exception to "dashboard only reads services").

### 6. #24 — Fix spec markdown links for GitHub  _(task / S)_
Rewrite `specs/**/*.md` links to repo-root files (`lv_conf.h`, `CMakeLists.txt`,
…) to `../../…` so they render on GitHub. (This file already follows that.)

---

## Notes
- `#364` blocked on `#363` (glyph) — sequence accordingly.
- Multiple `ui.c`/layout + service-strip touchpoints (`#365`, `#364`, `#250`,
  `#369`) — one coordinated layout pass.
- Close-out: [.korg-sprint-proposal](../../.korg-sprint-proposal) holds
  `korg:366` for `sprint-ship`.
