# 035 — Stop the flicker: host column hold and NO PATH debounce

Proposal korg:4593 covers WI 4592 and WI 4542. It ran as a karc leg
(kpidash-efbfe4) on kai under an overseer.

## Goal

Two short blips drew motion on the panel for nothing:

- **WI 4592.** kimac drops out of the host row for a few seconds and comes
  back, so the columns shift, the weather filler grows, then shrinks again.
- **WI 4542.** The 10G card flashes "`<host>` NO PATH" for one producer
  interval (~4 s), moving between hosts.

Both are timing rules in the pure layer, with tests.

## Premise check

| claim | verdict |
|---|---|
| An intermittent host's column goes the moment its 5 s health key expires | **holds.** `hostcols_select` skipped `CLIENT_PRESENCE_ASLEEP`, and `client_presence` is ASLEEP for an intermittent host the moment `online` is false. |
| Tiles would flash or gap inside the hold | **gone.** `redis_parse_telemetry_json` runs only when the key is present and `hosthist_record` likewise, so the tiles and sparklines already keep the last record. Tier 1 only needed the column itself not to change. |
| Other readers of `online`/`client_presence` could turn the hold into a false count or alert | **nothing to protect.** `hostcols.c` was the only reader of either. The stale registry (WI 4111) uses its own intermittent list from config, the status bar does not count hosts, and the weather width follows `hostcols_select` via `hostcols_used`. |
| The NO PATH flashes are single failed Internet echoes, not an expired key | **holds**, from both the code and the feed. See "4542: what the feed says". |

## 4542: what the feed says

The item's first step, done before writing code.

- **The code already rules out an expired key.** An absent `kdash:net:{host}`
  makes the column STALE and the headline `<host> STALE` ("no feed for N s").
  `<host> NO PATH` with "Internet unreachable from this host" is produced
  *only* by a live record with `reach.wan == false`.
- **The producer sends one echo.** `kpidash_client/net.py` runs
  `ping -n -c 1 -W 1 1.1.1.1` per ~5 s record, so one lost packet is one
  `wan: false` record.
- **The path loses about that many packets.** From kai, 300 echoes to
  1.1.1.1 at 0.2 s lost 5 (1.7 %), and 300 to the UDM (192.168.1.1) lost 0.
  Six hosts publish about 72 records a minute between them, which predicts
  roughly one single-miss NO PATH a minute somewhere on the card: Ken's
  "routinely, moving between hosts".
- **Sampled the feed:** `kdash:net:*` on rpi53's Redis once a second
  (`.scratch/netsample.sh`), 17:52:49–18:17:48 UTC on 2026-11-13: 25 minutes,
  1459 polls of six hosts, ~1795 distinct records. **13 records had
  `wan: false`, every one of them a single record** between good ones (the
  closest pair was 81 s apart), each seen by 4–5 polls (one record's
  lifetime), with `udm` and `peers` true. The key was **never missing**. No
  record had `udm: false` or `peers: false`. That is 0.7 % of records, in
  line with the packet loss, and spread over every host (kai 4, gratch 3,
  kubs0 3, cleo, kubsdb, rpi53 1 each).

So the flashes are **single failed Internet echoes, not an expired key**, and
the panel-side debounce is the right fix. Three consecutive failures by
chance at 0.7 % is about 4 × 10⁻⁷ per record: it does not happen.

## Decisions

- **Host column tiers (4592)** are one pure function, `hostcols_tier(c, now)`,
  measured from the host's `last_seen_ts`: LIVE (key present), HOLD (gone
  < `HOSTCOL_HOLD_S` = 30 s, drawn exactly as LIVE), NODATA (gone longer,
  column in place, red `NO DATA <age>`), GONE (an intermittent host gone
  ≥ `HOSTCOL_DROP_S` = 300 s: no slot). An always-on host never reaches GONE.
  Both constants sit in `protocol.h` beside the TTLs. `hostcols_select` gained
  a `now` argument, since who gets a slot now depends on the clock.
- **No stamp, no hold.** A host with `last_seen_ts == 0` (never seen since the
  panel started) is past every window: an asleep intermittent host gets no
  column invented from the admitted file, and an always-on one stays
  `NO DATA` as before.
- **A producer clock slightly ahead of the panel's** gives a negative age and
  reads HOLD, not NODATA. Correct for a fresh host.
- **NO PATH debounce (4542)** lives in `net_observe`, per the decision on the
  item: `wan_miss` counts consecutive failed **records**, keyed on `ts`,
  because the 1 s poll reads each ~5 s record about five times. Until it
  reaches `NET_WAN_MISS_STREAK` = 3, the stored record's `reach_wan` is drawn
  true; one good record clears it. A real loss shows on the third record,
  about 10 s after the first.
- **Not held back when the UDM probe failed too.** The gate caught this:
  `test_uplink_lost` feeds one record with UDM and Internet both down, and a
  blind mask drew the WAN tile up behind a lost gateway. A failed UDM
  explains the failed Internet and is shown at once, so the mask only applies
  when `reach_udm` is true.
- **UDM and fabric-peer probes stay immediate.** The proposal left this to
  the feed: apply the rule to them only if they showed the same single-miss
  flashes. In 25 minutes they showed none (and 0 of 300 echoes to the UDM
  were lost from kai), and `peers` is already "any peer answers". So
  debouncing them would hide nothing that happens and delay a real gateway or
  fabric loss. They stay immediate.

## What shipped

- `src/protocol.h`: `HOSTCOL_HOLD_S`, `HOSTCOL_DROP_S`.
- `src/hostcols.{h,c}`: `hc_tier_t`, `hostcols_tier`; `hostcols_select` takes
  `now` and skips only GONE; `hostcols_build` sets `nodata` only in NODATA.
  `src/ui.c` and `tests/render_host_columns.c` pass `now`.
- `src/netstate.{h,c}`: `NET_WAN_MISS_STREAK`, `net_host_t.wan_miss`, the
  streak in `net_observe`.
- Tests: `test_hostcols` gains `test_tiers` (every boundary, both host kinds,
  no stamp, clock skew) and `test_hold_keeps_column` (kimac misses a beat: no
  reflow and identical drawing for 30 s, NO DATA in place at 60 s, resumes in
  place, gone after 5 min while always-on stays). `test_netstate` gains
  `test_one_lost_echo_is_noise` (one record polled five times, a clearing
  record, two misses still quiet, the third shows, one good record clears,
  UDM and peers immediate); `test_wan_down` now feeds three records.
- Docs: ARCHITECTURE.md (data flow and host-columns sections, network
  health), CLIENT-PROTOCOL.md §8d (the reader's debounce; contract
  unchanged).

Gates: `just check` green; `just check-warnings-full` (whole tree, `-O2
-Werror`) clean.

## Live triggered tests (ship turn, after deploy)

Stop `kpidash-client` on one host for ~20 s (nothing changes), ~60 s (NO DATA
in place) and > 5 min on an intermittent host (kimac: its column goes), each
time restarting the client and confirming it publishes again. No cable pull,
no blocked traffic: the NO PATH streak is proven in the gate.

## Deployed

Deployed 2026-11-13 at 10:20 PST from kai. This was the ship turn, after the
overseer's clearance (korg:4593 comment 4631), which also ruled for the
UDM-failed exception. The release is squash merge `c10132f` (PR #38).

- **rpi53:** `scripts/deploy.sh` ran from merged `main`. The service is
  active with `NRestarts=0` and reports `c10132f (2026-11-13)`. The aarch64
  build and link printed no warnings (`.scratch/deploy-035-ship.log`,
  grepped). `just check-warnings-full` on `main` was clean. The journal shows
  no warnings or errors since the restart.
- **Live triggered tests, WI 4592.** Screenshots came through `krpidss`, in
  `.scratch/live035/`, with strips `kai-strip.png` and `kimac-strip.png`.
  Each client was restarted and seen publishing again (health key back
  within 1 s):
  - **kai (always-on), client stopped 20 s:** the shot at 15 s is identical
    to the baseline. The border is green, `up 23d 20h`, last values, no
    reflow.
  - **kai, client stopped 60 s:** at 42 s the column reads `NO DATA 41s`, red
    and dimmed, in place. At 61 s it reads `NO DATA 1m`. 10 s after the
    restart it is live again.
  - **kimac (intermittent), LaunchAgent booted out 342 s:** at 22 s it is
    unchanged. At 61 s and 272 s it reads `NO DATA 1m` and `NO DATA 4m` in
    place. At 342 s the column is gone, the row has closed up, and the
    weather filler has widened to four columns. After the `bootstrap` it is
    back in its slot. kimac was reached from kai with `ssh -o BatchMode=yes`,
    which returned rc 0, so it was awake.
- **WI 4542** was proven in the gate (`test_one_lost_echo_is_noise`), as the
  notes asked. No real traffic was blocked. Single failed echoes keep
  arriving at about 0.5 a minute (see the feed sample), so the panel now
  absorbs them all.
