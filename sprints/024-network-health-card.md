# Sprint 024: the network-health card, producer and panel

Proposal korg:4066, covering WI 4061 (the producer) and WI 4062 (the card). This
is slice 3 of program korg:4068 ("Network-health card on kpidash, replaces
Fortune"). Slice 1 (korg:4064) picked the look, "B · Rail". Slice 2 (korg:4065)
wrote the contract, `kdash:net:{host}` v1 (kdashdata CD-27). Slice 3.5 is cleo's
Windows producer. Slice 4 (korg:4067) rolls the client out to the fleet. This
slice ran as karc leg `kpidash-15d23f` on kai, headless, under `/overseen-sprint`.
Every live probe and capture ran **from kai**.

Read before starting:
- the overseer's brief in the proposal's notes, and comment 3991, which named
  kdashdata `6a928e3` as the bump target;
- WI 4059's design record (comment 3984) and its six captures. The mock-up source
  is `planning/netcard-mockups.html`, and `planning/netcard-brainstorm.md` sits
  beside it.

## Premise check

| claim | verdict |
|---|---|
| the contract is in kdashdata at the slice-2 merge | **holds**: `6a928e3` carries `contracts/schemas/kdash-net.schema.json` and CD-27 |
| `/proc/net/bonding/bond0` and `/sys/class/net` are readable as the client's user | **holds** on kai, kubs0, kubsdb and gratch, all read as `ken` over ssh |
| three NIC types on the fleet: AQC113 (kai, kubs0), X550 (kubsdb), AQC107 (gratch) | **holds**: PCI ids `1d6a:04c0` ×2, `8086:1563`, `1d6a:d107`. The bonding file is the same shape on all three drivers (atlantic, ixgbe) and both kernels (6.8, gratch's 7.0) |
| rpi53 has no bond | **holds**: a single `eth0` (macb) at 1000 Mb/s, default route via the UDM, `carrier_down_count` readable |
| Fortune is a pool widget in a 2×1 cell after the dev graphs | **holds**: `WIDGET_FORTUNE` in `layout_pool_place`, fifth slot on today's panel |
| the NAS panel (korg:3956) owns the other 2×1 slot | **not in this repo yet**. Nothing to reflow against today; the card takes Fortune's pool entry and nothing else moves |

The repo was dirty at start: the slice-1 design record (`netcard-brainstorm.md`,
`netcard-mockups.html`) was untracked. WI 4059's record names this repo as their
home and the brief builds from them, so they are committed as this sprint's second
commit (`be30dd7`). The commit is flagged to the overseer, not hidden.

## Decisions

### Producer memory: the brief's default, a small on-disk ring

`net-history.json` lives under the client's state dir: `$STATE_DIRECTORY`, else
`$XDG_STATE_HOME/kpidash-client`, else `~/.local/state/kpidash-client`. On the
fleet's system units (`User=ken`, HOME set) that is the last of the three. It
holds:

- **24 h of counter samples** (primary-slave LFC and rx_errors), one kept every
  five minutes, about 288 a day. The newest sample is provisional: a larger value
  overwrites it, because increments telescope, but a **decrease is always kept** as
  a reset, so the delta counts from it. The file is rewritten only when a sample
  is kept or a drop is recorded. That is roughly every five minutes, chosen
  because rpi53 runs off an SD card.
- **the last hour of drops**, as unix seconds. A drop is an increment of the
  primary slave's LFC on a bond, or of `carrier_down_count` on a single NIC. An
  increment seen across a gap longer than an hour is counted in the 24 h delta
  but not placed as a drop, because the card would put a tick at a moment it does
  not know.

**Aligned mid-sprint with the overseer's ruling** (korg:4066 comment 3995,
carried over from slice 3.5's Windows producer so that the two agree). Two
changes landed after 1.3.0 was published, so they ship as **1.3.1**:

- The 24 h delta's baseline is the **oldest sample inside the window**. 1.3.0
  anchored on the newest sample just outside it.
- **The last link state is not persisted.** 1.3.0 persisted the drop counter, so
  a reboot within the hour could log a phantom drop when the bond came up with
  its counter re-initialised. A drop is now an increment between two ticks of the
  same process, and the first tick after a start only sets the baseline
  (`test_a_restart_never_logs_a_phantom_drop`).

The rest already matched the ruling: an on-disk ring under the state dir, one
sample per 5 min kept for 24 h, the last hour of drops, an atomic write only on
change, and a reset counted from the reset. One difference from the Windows
producer is deliberate and inside CD-27: a Linux drop is an **LFC increment**,
not an observed up→down. LFC counts flaps that happen between two 5 s ticks, which
an up/down comparison would miss. The contract names both forms.

**Where the file lives**: `~/.local/state/kpidash-client/net-history.json` for
user `ken` on every Linux host. That covers kai, kubs0, kubsdb and gratch (uv tool
installs under system units) and rpi53 (the source venv at
`~/src/kpidash/clients/kpidash-client/.venv`, also `User=ken`). None of these
units sets `StateDirectory=`, so the XDG fallback applies.

A delta is `null` only on the very first sample, before any window exists. After
a restart the ring is still there, so the card never shows `(+0)` for a window the
producer did not see. That property is tested (`test_history_survives_a_restart`).

### Probes: the system `ping`, concurrently, bounded

`net.ipv4.ping_group_range` is `1 0` on the fleet, so unprivileged ICMP sockets
are off. `/usr/bin/ping` carries the capability. Every target is one `ping -c 1
-W 1`, all launched at once and reaped against a 2.5 s deadline; stragglers are
killed. The loop is its own daemon thread, so a dead target never delays telemetry
(`test_probe_all_bounds_a_dead_target`). Peers are the four servers' bond
addresses, minus the host's own, overridable as `[net] peers`. The UDM is
`192.168.1.1` and the Internet target is `1.1.1.1`.

### The fabric NIC is the primary slave, whichever leg is active

`lfc`, `rx_errors` and `bytes_per_s` are the primary (10G) slave's, as CD-27
rules. `leg` and `speed_mbps` are the active slave's. So a host failed over to 1G
reports `1g` / `1000` and keeps reporting its 10G NIC's counters, and near-zero
throughput on it is correct. With no primary configured, the fastest slave stands
in. A host with no bond uses the default-route interface.

### Freshness: the host cards' rule, and no second one

The key is absent this poll means the publisher is gone (EX 15, written every
~5 s). The column keeps the last record and renders it hatched, with
`STALE · n s` and grey pills. The verdict reads `<host> STALE` (warn). A record
that fails the contract counts as absent, the same as a missing key. WI 2944's
soak is untouched: this card reads its own key and changes nothing about the host
cards.

**"No feed" is a third state, and it is not a fault.** A host that has not
published since the dashboard started gets a grey, hatched `NO FEED` column, and
the headline does not change: the summary line just lists `· kubs0 no feed`. This
is a call the brief did not make explicitly, so it is flagged. Before slice 4 rolls
the client out, three of the five columns are in this state, and a permanent
yellow headline over "slice 4 hasn't run" would be noise. The cost: after a
dashboard restart, a host that went down before the restart reads "no feed" rather
than "stale" until it publishes again. The host cards solved the same problem with
the admitted-hosts file (WI 3012), and that is the fix if this ever matters.

### Verdict precedence

Fabric-wide states come first, because they explain every column at once:
**UPLINK LOST** (live hosts reach peers, none reaches the UDM), then **WAN DOWN**
(the UDM is reachable, no host reaches the Internet). After those, the worst
column is the headline: `DOWN` and `NO PATH` (crit), then `STALE`, then
`FLAPPING`, `ON 1G`, `LINK DROP` and `RX ERRORS` (warn). When more than one column
has a problem, the summary line ends `· +N more`. A healthy fabric reads
`ALL 10G` when every live bonded host is on 10G. RTT tiles are medians over the
live hosts that reach the target.

### Throughput history: 60 per-minute means

The brief asks for "the 60-sample ring per host", and the design labels the
sparkline "Mb/s · 60 min". Sixty one-second polls would be one minute, so each
sample is a minute's mean, and the minute in progress is drawn as the newest
point. Drop ticks are placed by their timestamp across the same hour. History
freezes while a column is stale.

### Fonts: everything non-ASCII goes in the bold face

The regular Montserrat 14/16/20 are LVGL's built-ins and carry ASCII only. The
bold sizes are generated with Latin-1 and the dashes. So every line that can
carry `·` or `—` is a spangroup, and the renderer draws any non-ASCII character
in the bold face of the same size. The same pass colours the speed tokens
(10G teal, 5G sapphire, 1G yellow, DOWN red). The mock-up's `✕` in the tiles is in
no font, so a lost tile reads `DOWN` / `LOST` in red. This avoided regenerating
fonts, which would have meant Node.js, new committed artifacts and a
`check-fonts` change.

### Fortune: compiled out, not deleted

`KPIDASH_WIDGET_FORTUNE 0` in `src/ui.h`. The fortune engine's `fortune_init` is
compiled out too, so the dashboard no longer shells out to `fortune` on a timer
for a widget that does not exist. `WIDGET_NETCARD` sorts after `WIDGET_GRAPH`, so
the dev graphs keep their cells (`test_netcard_takes_fortunes_slot`).

## What shipped

- `lib/kdashdata` `bb29b69` → **`6a928e3`** (`fd491c7`).
- **kpidash-client 1.3.1**, published to the homelab package store (1.3.0 was
  published first and is superseded; see the memory ruling above):
  `kpidash_client/net.py`, a `_net_loop` thread, `RedisClient.write_net`, `[net]`
  config, and `kpidash-client net` (one record, no Redis, no history file).
- `src/netstate.{h,c}`, the pure model, plus `src/widgets/net_card.{h,c}` (B · Rail),
  `redis_poll_net` (one `MGET` per second), `KPIDASH_NET_HOSTS` (default
  `kubs0,kubsdb,kai,gratch,cleo`) and `WS_COLOR_{TEAL,SAPPHIRE,LAVENDER}`.
- Tests: 40 Python tests (`tests/test_net.py`) against real bond0 captures from
  all three NIC types, with every record shape validated by kdashdata's own
  `jsonschema_mini`. `tests/test_netstate.c` requires every schema example to
  parse and every counterexample to be refused, read from the schema file at run
  time, and covers each design state. `test_config` and `test_layout_pool` gain the
  new cases. `check-client` now inits `lib/kdashdata`, so a missing submodule fails
  rather than skips.
- Docs: CLIENT-PROTOCOL.md §8d, an ARCHITECTURE.md data-flow entry, the client
  README, and a note that §6 Fortune is compiled out.

## Repaired in passing

- `just check-client` read nothing from kdashdata before. It now depends on
  `_kdash`, because `test_net.py` does. Without that, a clean clone would have
  failed the client gate on a missing schema. Proven by `just check`.

## Cross-repo changes made

None. The contract was consumed as shipped. kai's client was upgraded to 1.3.0
with `uv tool upgrade`, which k-homelab's recipe documents as an operator action
(`kpidash_min_version` is a floor, not a pin). It is listed under Deployed, not
here, because it changed no repo.

## Triggers fired (no soak)

- **Stop one publisher** (the leg's own test, WI 4062). At 18:35 PDT the leg ran
  `sudo systemctl stop kpidash-client` on kai. `kdash:net:kai` expired about 12 s
  later, and about 25 s after the stop the panel read `kai STALE` (yellow).
  kai's column showed `STALE · 25 s`, hatched, with grey pills and the last
  record kept, never green. The client was restarted at once. Capture
  `netcard-live-kai-stale.png` (img-ff2) is on WI 4062.
- **Pull a 10G cable**: Ken's, not fired here. The instruction is in the wrap-up
  handoff on korg:4066. kai is the host to pull, because it is the only bonded
  host whose producer is live before slice 4.

## Deployed

2026-10-26, from kai, against rpi53. All probes and captures ran from kai.

- **kpidash-client 1.3.0, then 1.3.1**, were published to the homelab package
  store (`just publish`). **kai** was upgraded with `uv tool upgrade
  kpidash-client` (1.1.0 → 1.3.0 → 1.3.1) and its system unit restarted after
  each. **Slice 4 pins 1.3.1.**
  `kdash:net:kai` is live on rpi53:6379, TTL 15, `nic: AQC113`. No other host was
  touched; slice 4 (korg:4067) rolls the client to kubs0, kubsdb, gratch and rpi53.
- **Dashboard**: `scripts/deploy.sh` three times, the last reporting `c6e1abc`.
  The aarch64 link printed no warnings on any of them
  (`.scratch/deploy-024{,b,c}.log`, grepped). `just check-warnings-full` was clean
  before each.
- **Live on the panel**: the card sits in Fortune's cell, row 3 columns 3-4,
  after the four dev graphs, which did not move. kai (10G, AQC113) and cleo (5G,
  RTL8126, slice 3.5's producer, already publishing) are live. kubs0, kubsdb and
  gratch read `NO FEED` until slice 4. The headline is `ALL 10G`, with UDM 0.3 ms
  and WAN 13-14 ms.
