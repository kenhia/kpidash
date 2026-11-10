# Sprint 028: update-issue card, gratch NAS panel, khlenv retry

Proposal korg:4371 covers WI 3642, WI 3956 and WI 4353. It is a slice of
backlog-drawdown program korg:4368 and ran as karc leg `kpidash-0f933a` on kai,
headless, under `/overseen-sprint`. The overseer's calls are in the proposal's
`notes`. Below, I follow each one or say why I did not.

## Premise check

| claim | verdict |
|---|---|
| 3642: the contract is `kdash:update:{host}:{kind}`, kpidash has no reader yet | **holds**. kdashdata `4837473` registers it, and nothing in `src/` read it |
| 3642: document it as CLIENT-PROTOCOL **§8d** | **drifted**. §8d has been the network card since sprint 024, so this is **§8e** |
| 3956: the deps are done and `kdash:nas` v1 has landed; gratch is Ubuntu + ZFS | **holds**. Read on gratch: one pool `nas`, eight disks, `privileged.json` matches the registry's mapping field for field |
| 3956: "the first slot after the dev graphs" is free | **gone, as comment 4011 warned**. The pool has six 2×1 slots, and five dev graphs (gratch, kai, kimac, kubs0, kubsdb) plus the net card fill them (screenshot `.scratch/before-028.png`). See the decision below |
| 4353: the client fell back at boot and never retried | **holds, with the mechanism found**. gratch's journal shows the 2026-11-05 23:24:18 fallback, and the cache still read `fetched_at 11-04`, `last_failover.at 11-05`. Re-resolve only ran from `connect()`. On 11-06 there was a Redis timeout, but `reconnect_on_failure()` pinged first, and redis-py's ping reconnects by itself, so `connect()` never ran |
| 4353: ksandbox and krgate may be in the same state | **gone**. Neither host runs kpidash-client (no unit is loaded) and neither has a khlenv cache as ken or as root. I probed from kai, and ssh connected to both |

## Decisions

### WI 4353: retry while on a fallback

- **The cache file is the signal.** khlenv 0.3.0 writes a sticky
  `last_failover` record into its D6 cache. A live answer moves `fetched_at`
  past it. kmon reads exactly that comparison. `khlenv_on_fallback()` reads
  the same file through khlenv's public `cache_path`, so the client and kmon
  cannot disagree about whether the client has recovered. It uses `>=` where
  kmon uses `>`: a tie in the same second costs one extra retry instead of
  one missed retry.
- **A new daemon thread checks every 60 s** (`KHLENV_RETRY_S`). While the
  cache reads fallback, the thread re-resolves. A live answer clears the
  fallback, and a different endpoint triggers a reconnect. When the cache is
  not on fallback, the check is one local file read. During a khlenv outage
  khlenv logs each failed attempt, and the client logs only the recovery.
  Nothing runs under a `[redis] host` override.
- **I did not order the unit after `tailscaled` as well.** The overseer called
  it the weaker second line, and it is: `active` is not "DNS ready", and the
  retry covers the case completely.
- `khlenv>=0.3.0`, because 0.1.0 writes no `last_failover`. The lock moved
  from 0.1.0 to 0.3.0, and gratch already runs 0.3.0.

### WI 3642: the update-issue card

- **The staleness reader, re-cut.** It has its own registry
  (`update_registry_*`), with the same pending/live split and the same CD-18
  rule: the key is the flag, an unreadable payload costs only the detail, and
  only a scan that completes may take a card down. I kept it as a copy rather
  than generalising the staleness registry. The two differ in exactly the
  places a shared version would need flags: the excluded host, the
  intermittent grace, and the issue line.
- **Each field stands on its own.** An ISO `since` loses the card's ordering
  but keeps the line. A missing `issue` renders `<kind>: detail unknown`. An
  unknown kind renders by its line. rpi53 is included.
- **Strip order** is `[services][stale][update][apt-temps]`. Stale and update
  cards both mean "a host needs Ken", so they sit together.
- The body is set at 16 px and wrapped. Lines can run to 200 characters, and
  there is no input to scroll with, so an overlong body is clipped at the
  bottom. Issues are sorted oldest first, so the clipped ones are the newest.

### WI 3956: the NAS panel takes gratch's own graph slot

The panel needed a slot and none was free. Comment 4011 asked whoever planned
this to "decide which generic graph the NAS panel replaces". I chose
**gratch's own**. The WI puts gratch's activity graph *inside* the panel, so
gratch's separate dev graph would show the same thing twice. Every other host
keeps its graph, and the net card keeps its slot. In practice:

- A `WIDGET_NAS` kind sorts after `WIDGET_GRAPH` and before the net card. Once
  `kdash:nas:gratch` has published during this run, ui.c drops gratch's graph
  request and adds the panel. The result reads kai, kimac, kubs0, kubsdb,
  **gratch NAS**, net. That is "the first slot after the dev graphs".
- Until gratch has published, gratch keeps its ordinary graph. A panel restart
  while gratch's producer is dead therefore shows the graph, not a STALE panel.
  This is the same one-restart gap sprint 027 closed for the net card with
  the admitted file. I did not persist it here. The panel has one host, and a
  dead producer also takes the graph's own data with it, so the graph reads
  NO NEW DATA either way.
- **The activity graph is CPU % and RAM %, not disk and network throughput.**
  The WI says "disk/net activity", but `dev_telemetry` carries CPU, RAM and
  GPU only, and no contract carries gratch's disk or network throughput.
  Adding them means changing kdashdata's `kpidash-client-dev-telemetry`
  schema, which is another repo's contract, so I raised it in the wrap-up
  rather than doing it here.
- **The host is a constant**, `KPIDASH_NAS_HOST "gratch"`. It is not a config
  list, because there is one NAS.
- **Producer auto mode.** The `[nas]` loop runs on every host but publishes
  only once `/run/gratch-ui/privileged.json` has existed during the run. Only
  gratch ever writes the key, and gratch needs no config edit. Once seen, a
  vanished or unreadable file publishes "collector down", as the contract asks.
- **Levels** are in §8f. Standby and a constant reallocated count are grey,
  not faults. The WI says a standby IronWolf is normal, and sdc's 8
  reallocated sectors have no history to show a rise against.

## What shipped

- `clients/kpidash-client` **1.5.0**:
  - `endpoint.khlenv_on_fallback`, `RedisClient.refresh_endpoint_if_fallback`
    and the `_khlenv_loop` thread.
  - `nas.py`, with the `[nas]` config, `write_nas` and the `_nas_loop` thread.
  - Tests: `test_endpoint.py` adds 12 cases, including an end-to-end run
    against a stub khlenv starting from gratch's real cache. `test_nas.py`
    validates the projection of a real gratch capture
    (`tests/fixtures/gratch-privileged-2026-11-10.json`, serials stripped)
    with kdashdata's `jsonschema_mini`.
- Dashboard:
  - `kdash:update` reader, registry, card and paint block, with
    `tests/test_update_card.c`.
  - `nasstate.c` with `tests/test_nasstate.c` (fed the schema's own examples),
    `widgets/nas_card.c`, and the pool wiring.
- Docs: CLIENT-PROTOCOL §8e and §8f, ARCHITECTURE items 11 and 12, and the
  client README.

Gates: `just check` (C 15/15 ctest, client 180 passed, units, scripts, font coverage) and `just check-warnings-full`
(the whole tree at `-O2 -Werror`) are clean.

## Preview on rpi53 (before review, rolled back)

The panel and the card are LVGL code no gate renders, so I previewed the
branch on rpi53 before asking for the green light. The preview ran from
2026-11-09 18:04 to 18:20 PST, from kai.

- `scripts/deploy.sh` deployed the branch build, which reported `879c07f`.
  For about 30 s I published `kdash:nas:gratch` from kai, using
  `nas.project()` over gratch's live `privileged.json` with `EX 15`, so it
  expired by itself. I also wrote two hand-made update keys:
  `kdash:update:rpi53:reboot-awaiting`, whose issue line said it was a
  preview key, and `kdash:update:rpi53:held` set to `not json`.
- **What the screenshots show** (`.scratch/preview-028*.png`):
  - The NAS panel sits between kubsdb's graph and the net card, in place of
    gratch's graph.
  - The pool bar reads `989 GB / 5.9 TB · 17%`, with `scrub 2 d ago · 0
    err`. The root bar comes from telemetry.
  - All eight disk rows render, and sdb and sdd show grey `standby` stripes.
  - The update card reads `rpi53 update` over the issue line and
    `held: detail unknown`. Both CD-18 paths are visible.
- **Once the NAS key expired**, the header read `STALE · 15 s` in red, and the
  border and stripes went grey with the values kept in view.
- **Two fixes came out of it.** Pool-bar text was light on teal and
  unreadable, so it is now dark on the host cards' light bar background.
  SSD and NVMe rows said "spinning", and spin state is now an HDD-only word.
- **Restored.** I deleted both update keys (0 `kdash:update:*` remain) and
  deployed `origin/main` (`dd089bb`) from a temporary worktree. It reports
  `dd089bb-dirty` only because the worktree's submodules were symlinks.
  `--rollback` would have restored the first preview build, not main. The
  Pi's `~/kpidash.prev` is therefore a preview binary until the ship deploy
  rotates it.

## Repaired in passing

- **An aarch64-only compile warning this sprint introduced.** The cross-build
  printed `-Wstringop-overread` on ui.c's `g_snap` scan. Neither x86 gate
  sees this class, and CLAUDE.md warns about it. GCC lost the
  `n_hosts <= GRAPH_HOST_MAX` bound across the snapshot call, so the bound
  is now an explicit clamp. `cmake --build build-pi` is silent again.

- `docs/ARCHITECTURE.md` item 10 said `kdash:stale` was "the only feed kpidash
  reads that it does not own". That has been false since `kdash:net` arrived
  in sprint 024. I reworded it.

## Follow-ups (for the overseer to route, not filed)

- **Disk and network throughput for the NAS activity graph** needs a field in
  kdashdata's `dev_telemetry` contract or a new one. That is kdashdata's
  decision.
- **gratch's `config.toml` still lists `/mnt/cache` and `/mnt/disk1`** as host
  card disks. Both paths still exist, so it is not broken, and it is a
  question of what gratch's host card should show. That is Ken's call.

## Deployed

2026-11-09 18:09–18:11 PST (02:09–02:11 UTC 11-10), from kai, on the ship
turn. That followed the overseer's clearance (korg:4371 comment 4387, which
accepted all three calls) and the squash merge `2a68da7` (PR #30).

- **`just publish`**: kpidash-client **1.5.0** wheel and sdist are in the
  package store.
- **gratch**:
  - `uv tool upgrade kpidash-client` took it from 1.3.1 to 1.5.0, then
    `sudo systemctl restart kpidash-client`. It is active with
    `NRestarts=0`.
  - **4353 proof, from the client's own cache.**
    `~/.cache/khlenv/kpidash-client.json` read `fetched_at
    2026-11-04T01:12:53Z` before the restart and `2026-11-10T02:09:44Z`
    after, against `last_failover.at 2026-11-05T23:24:18Z`. The client is
    off the fallback, by kmon's own comparison.
  - **Producer live.** `kdash:nas:gratch` is on rpi53 with TTL ≤15,
    `source_stale false`, 1 pool and 8 disks.
- **rpi53**:
  - `scripts/deploy.sh` ran from merged `main`. The service is active with
    `NRestarts=0` and reports `2a68da7 (2026-11-10)`. The aarch64 build
    printed no warnings (`.scratch/deploy-028.log`, grepped).
  - `~/kpidash` matches `build-pi/kpidash`
    (md5 `5d525a36ea6908035aa2dc7a82a25a2c`). `~/kpidash.prev` is the
    `dd089bb` restore build, so no preview binary is left on the Pi.
- **Live screenshot** (`.scratch/live-028.png`), attached to korg:4371 as
  img-112b (full panel) and img-112c (the NAS panel):
  - The panel shows the real producer's data in gratch's former graph slot,
    between kubsdb and the net card.
  - It reads `collected 3 m ago`, `nas · ONLINE`, `989 GB / 5.9 TB · 17%`
    and `scrub 2 d ago · 0 err`.
  - sdb and sdd show neutral `standby`. sdc shows `realloc 8`. The flash
    rows no longer say "spinning".
- **Update card**: no live key exists yet, because the writer is the
  sibling k-homelab slice's apt runner. The reader was verified with hand
  keys during the preview (above).
