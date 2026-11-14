# KPI Dashboard — Client Protocol Specification

> **Version**: 2.0 (Sprint 002)
> **Transport**: Redis 7.x key/value operations
> **Auth**: `REDISCLI_AUTH` environment variable, from the host's one copy at
> `/etc/khomelab/secrets.env`
> **Encoding**: JSON values in Redis strings/hashes

This document is the canonical reference for all Redis key names, value
formats, TTLs, and access patterns for the kpidash system. All components
(dashboard C binary, Python client daemons) MUST conform to this
schema. See also `sprints/001-mvp-dashboard/contracts/redis-schema.md` for the
authoritative source during development.

---

## Key Naming Conventions

- All keys are prefixed `kpidash:` — with one exception, the `kdash:stale:*`
  feed in §8c, which the dashboard **reads** but does not own (it belongs to
  kdashdata and is written by the fleet deployers).
- Separator: `:`.
- Hostnames are lowercase, matching the system `hostname` output.
- JSON field names use `snake_case`.

---

## 1. Client Registry

| Key | Type | Written by | Read by | TTL |
|-----|------|-----------|---------|-----|
| `kpidash:clients` | SET | client (startup) | dashboard | none |

```
SADD kpidash:clients {hostname}
```

Dashboard enumerates clients with `SMEMBERS kpidash:clients`.

**The set is append-only, so membership alone does not earn a card
(sprint 022, WI #2524).** `redis_client.py` SADDs on every health write and
nothing in the fleet ever SREMs, so a host that is registered once stays
registered forever — `kwork` sat in the set with no `kpidash:client:kwork:*`
key of any kind, and the panel carried a permanently-down card for it, on a
screen with no input devices where "down" is exactly how a genuinely dead
machine reads.

The dashboard therefore admits a member the first time it actually publishes:

- **Never published** (no health *and* no telemetry, ever, in this dashboard
  run) → **no card.** It costs no `MAX_CLIENTS` slot and evicts nobody.
- **Published, then stopped** → **keeps its card**, which goes offline. Every
  client key is TTL'd (health 5 s, telemetry 15 s), so "no data this cycle" is
  any host fifteen seconds dead; dropping the card on that would turn every
  real outage into a host that silently vanishes, which is precisely the
  failure WI #902 exists to prevent.

Nothing is removed from the set, deliberately: a `kwork` that starts
publishing tomorrow appears on the panel by itself, with no operator step.

**"Has published" survives a restart, and it does so outside this protocol
(WI #3012).** Every client key is TTL'd, so Redis cannot answer "has this host
ever published" — a host dead for a minute and a host that has never existed
look identical. The dashboard therefore keeps its own small file of admitted
hostnames and loads it at startup, so a host that is down when `kpidash`
starts still gets its card and goes RED.

| | |
|---|---|
| **Path** | `/var/lib/kpidash/admitted`, overridable with `KPIDASH_STATE_FILE` |
| **Format** | one lowercase hostname per line — readable and repairable with `cat`. The headerless lines at the top are the host cards' set; a `[net]` line starts the network card's (§8d, WI #4085). A section a build does not know is skipped |
| **Written** | only when a host is admitted that was not admitted before (to either set); never on a timer. Both sets are rewritten together |
| **Bound** | `MAX_CLIENTS` (32 since sprint 029), the same bound as the host registry |
| **Pruning** | none, like the set itself |
| **Missing or corrupt** | starts empty; individual bad lines are skipped, not fatal |

This is **dashboard-local state, not part of this contract.** No key was added
to the `kpidash:client:*` namespace that kdeskdash also reads, and no
publisher has to change. A different consumer of this protocol is free to
solve the same problem differently, or not at all.

**One-time consequence of the sprint-022 deploy.** The first start after the
deploy finds no file, so the admitted set begins empty and each host is
re-admitted the first time it publishes — within seconds for a live
publisher. A member of `kpidash:clients` that happens to be **silent during
that first start** therefore has no card until it next publishes, where
before it would have shown a down card. It is a one-boot window and it never
recurs. Measured at the time of the sprint: the set held six members —
`kai`, `kubs0`, `kubsdb`, `rpi53`, `cleo` all publishing, and `kwork`, which
is the host this whole rule exists to keep off the panel.

---

## 2. Health

| Key | Type | Written by | Read by | TTL |
|-----|------|-----------|---------|-----|
| `kpidash:client:{hostname}:health` | STRING (JSON) | client (~3 s) | dashboard | **5 s** |

```json
{
  "hostname": "kubs0",
  "last_seen_ts": 1743292800.123,
  "uptime_seconds": 86400.5,
  "os_name": "Ubuntu 22.04.5 LTS"
}
```

`uptime_seconds` is optional. `os_name` is optional. It comes from `/etc/os-release`
`PRETTY_NAME` on Linux and from `platform.mac_ver()` on macOS (`"macOS 27.0"`). The
fallback everywhere else is `platform.system() + " " + platform.release()`.
Send the full string: the dashboard shortens it for display only (sprint 031,
"Debian GNU/Linux 13 (trixie)" is drawn as "Debian 13"), so producers should not
trim it themselves.
Key absence (TTL expired) = client offline.

`availability` is optional (sprint 023, WI #3132). The only value is `"intermittent"`,
which a host that sleeps by design sends, from `[client] availability` in its
`config.toml`. When it is absent the host is always-on, and every host that existed
before this field sends no `availability`. The dashboard keeps the **last** value it
saw after the key expires, because the expiry is exactly when the value matters. An
offline host that last said `intermittent` is drawn grey **asleep**, and any other
offline host is red **offline**. Any other value, or a non-string, counts as absent,
so a typo shows an outage instead of hiding it. The flag lives in dashboard memory
only. After a dashboard restart a host that is asleep shows red until it next
publishes. kdashdata's schema (`kpidash-client-health`) is `additionalProperties:
true`, so this field is contract-legal there without being listed.

```
SET kpidash:client:kubs0:health '{...}' EX 5
```

---

## 3. Telemetry

| Key | Type | Written by | Read by | TTL |
|-----|------|-----------|---------|-----|
| `kpidash:client:{hostname}:telemetry` | STRING (JSON) | client (~5 s) | dashboard | 15 s |

```json
{
  "hostname": "kubs0",
  "ts": 1743292800.456,
  "cpu_pct": 45.2,
  "top_core_pct": 78.1,
  "ram_used_mb": 6144,
  "ram_total_mb": 16384,
  "gpu": {
    "name": "NVIDIA GeForce RTX 4090",
    "vram_used_mb": 3072,
    "vram_total_mb": 24576,
    "compute_pct": 32.5
  },
  "disks": [
    {
      "label": "root",
      "type": "nvme",
      "used_gb": 237.4,
      "total_gb": 953.9,
      "pct": 24.9
    }
  ]
}
```

`gpu` may be `null` or omitted when no GPU present. `type` values: `"nvme"`,
`"ssd"`, `"hdd"`, `"other"`.

---

## 4. Activities — retired

## 5. Repo Status — retired

## 6. Fortune — retired

> **Retired, producers and all.** The dashboard stopped reading these feeds in
> sprint 032, and kpidash-client **2.0.0** (sprint 036) stopped writing them:
> the daemon's repo scanner (`kpidash:repos:{host}`), the `activity start|done`
> CLI (`kpidash:activities`, `kpidash:activity:{uuid}`), and `fortune push`
> (`kpidash:fortune:pushed`; `kpidash:fortune:current` was the dashboard's
> own). The `kpidash-mcp` package, the other activity writer, was deleted in
> the same sprint. Nothing in the fleet writes or reads any of these keys; the
> leftovers on rpi53 are deleted as part of the 2.0.0 rollout. The schemas
> are on the git tag `last-with-dev-graphs-fortune`, in this file.
>
> The section numbers are kept so the numbering of everything after stays
> stable.

---

## 7. Status Messages

| Key | Type | Written by | Read by | TTL |
|-----|------|-----------|---------|-----|
| `kpidash:status:current` | STRING (JSON) | dashboard | clients | none |
| `kpidash:status:ack:{message_id}` | STRING (`"1"`) | client CLI | dashboard | 60 s |

```json
{
  "message_id": "550e8400-e29b-41d4-a716-446655440000",
  "severity": "warning",
  "message": "kpidash: 2 host(s) past the 30-slot host columns limit",
  "ts": 1743292802.0
}
```

**Acknowledge flow**:
1. `GET kpidash:status:current` → extract `message_id`
2. `SET kpidash:status:ack:{message_id} 1 EX 60`
3. Dashboard detects ack on next 1 s poll → advances queue

---

## 8. Dev Telemetry (Sprint 002)

| Key | Type | Written by | Read by | TTL |
|-----|------|-----------|---------|-----|
| `kpidash:client:{hostname}:dev_telemetry` | STRING (JSON) | client (~1 s) | kdeskdash (not the dashboard since sprint 033) | 5 s |

```json
{
  "hostname": "kubs0",
  "host": "kubs0",
  "ts": 1743292800.789,
  "cpu_pct": 12.3,
  "top_core_pct": 45.6,
  "ram_used_mb": 6144,
  "ram_total_mb": 16384,
  "gpu_compute_pct": 8.5,
  "gpu_vram_used_mb": 431,
  "gpu_vram_total_mb": 16376
}
```

High-frequency telemetry stream, written at `dev_interval_s` (default 1 s,
configurable in `[client]` config section). Contains 5 metrics (GPU compute,
CPU avg, CPU top, VRAM, RAM). It was written for the per-host dev graphs,
which sprint 032 removed. The NAS card's CPU/RAM chart kept gratch's key
polled until sprint 033 dropped the chart, so the dashboard reads no
`dev_telemetry` key at all now. Every host's key is still read by kdeskdash
(`src/dev_telemetry.c` there), so the producer stays.

**Sprint 006**: The `host` field (default `"(legacy)"` when absent) names the
source host. The dashboard's per-host series that routed on it went with the
last reader in sprint 033.

---

## 8a. Service Status (Sprint 006, schema revised in Sprint 007)

| Key | Type | Written by | Read by | TTL |
|-----|------|-----------|---------|-----|
| `kpidash:services:{name}:{host}` | STRING (JSON) | manual / `kpidash-client service-status` | dashboard | **none** |

```json
{
  "ts": 1743292800.789,
  "state": "ok",
  "text": "API responding",
  "host": "kubs0",
  "icon": 3
}
```

| Field | Type | Required | Notes |
|-------|------|----------|-------|
| `ts` | float | yes | Last payload timestamp (unix seconds) |
| `state` | string | yes | One of `ok`, `unhealthy`, `maintenance`, `down`, `unknown` |
| `text` | string | yes | Short status text shown on the card, ≤128 bytes. See **Which characters are safe** below |
| `host` | string | no | Echo of the host segment for display; the **key** is authoritative |
| `icon` | int | no | Optional icon index into `lv_font_icons_56` |

**Sprint 007 schema**: the Redis key always has 4 colon-separated segments:
`kpidash:services:<name>:<host>`. The `<host>` segment is required; clients
that do not specify a host write the sentinel `_` (a single underscore,
invalid as a POSIX hostname). The dashboard treats `_` as "no host" and
hides the host line on the card. Identity is `(name, host)` — the same
service name on two different hosts produces two separate cards. Keys not
matching the 4-segment pattern (e.g. legacy `kpidash:services:foo` from
sprint 006) are silently ignored.

**Migration from sprint 006**: any pre-existing single-segment keys must
be deleted:

```bash
redis-cli --scan --pattern 'kpidash:services:*' | xargs -r redis-cli DEL
```

Dashboard polls `SCAN MATCH kpidash:services:*:*` each cycle and renders one
status card per (name, host) pair in the footer strip, sorted by name then
host. Card border colour reflects state via the truth table in
`sprints/006-layout-refresh-status-cards/data-model.md`: `DOWN`/`UNKNOWN` →
GRAY (sticky for DOWN); other states → their colour iff
`(now − ts) < ` the service's freshness window, else RED.

#### Which characters are safe in `text` (sprint 022, WI #2646)

The panel renders with a font built from a fixed glyph set — there is no
fallback font and no substitution. A character outside the set draws as an
empty box, and until sprint 022 it also wrote
`lv_draw_letter: glyph dsc. not found for U+…` to the journal **on every
redraw**: 3,844 lines in two hours for a single `·` on one card. The warning
is now deduped per codepoint (`src/logfilter.c`), so the cost of an
unsupported character is a box and one log line, not a flood — but it is
still a box.

**Safe, and covered by the gate:**

| Set | Notes |
|---|---|
| ASCII printable, `U+0020`–`U+007E` | |
| Latin-1 Supplement, `U+00A0`–`U+00FF` | Includes `°` `·` `©` `±` `«` `»` `¼` `½` and the accented letters |
| `–` `—` (`U+2013`, `U+2014`) | En and em dash |
| `'` `'` `"` `"` (`U+2018`–`U+2019`, `U+201C`–`U+201D`) | What a smart-quoted apostrophe or quote becomes |
| `•` (`U+2022`) | Bullet, commonly used as a separator |
| `…` (`U+2026`) | What an editor makes of `...` |
| `U+F300`–`U+F381`, `U+F1D2`–`U+F1D3` | Nerd Font icons, reachable via the `icon` index — not by typing them |

**Not safe:** anything else. Most often emoji, arrows (`→` `⇒`), box-drawing,
CJK, and the mathematical symbols that look like ASCII (`×` is Latin-1 and
fine; `✕` `✓` `✗` are not).

The set is declared once, in `RANGE` in `fonts/generate.sh`, and
`just check-fonts` asserts the committed `fonts/*.c` actually carry it. To
widen it: edit `RANGE`, re-run `cd fonts && bash generate.sh`, commit the
regenerated `.c` files, and update this table. All four, or the gate fails —
which is the point, since the generated fonts are committed artifacts and
nothing else would notice them going stale.

**This is a rendering policy, not a payload rule.** The dashboard stores and
compares whatever bytes it is given; it is only the drawing that is limited.
Publishers that want to be certain can stay inside ASCII, which every
version of the panel has rendered.

#### Freshness windows (sprint 018, WI #902)

The window is a **consumer-side rendering policy**, not part of this contract:
there is no cadence field in the payload, and publishers do not declare one.
The dashboard picks it from the service `name`:

| Service name | Window | Why |
|---|---|---|
| `kmon` | **26 h** (`SERVICE_DAILY_FRESH_SECONDS`) | Publishes daily; 24h refresh + 1h DST + 1h jitter |
| anything else | **75 s** (`SERVICE_FRESH_SECONDS`) | ~60s nominal cadence + 15s grace |

This section previously documented the default as `60.0 s`; the code has used
`75.0` since sprint 006 (60s nominal + 15s grace). The code was right and the
prose was stale — corrected here rather than changing the behaviour.

A daily feed under the 75s default would be RED from about a minute after every
successful run, which makes "healthy" and "dead for a week" render identically.
That is precisely the failure the kmon card was commissioned to catch: kmon died
silently for 10 days (2026-07-23 → 2026-08-01) while the dashboard showed a
stale GREEN "OK" from before the break. **Absence of fresh data renders as
alarming, never as last-known-good.**

Adding a second daily feed means adding a row to `g_service_windows[]` in
`src/registry.c`. Deliberately not generalised further on one consumer.

**What "absence" covers, and what it does not (sprint 022, WI #2306).** It
covers a service whose key exists and has aged past its window: that goes RED,
and because `kpidash:services:*` keys are TTL-less, a publisher that dies
leaves its last key behind forever — so the ten-days-silent case that
commissioned WI #902 is covered. It does **not** cover a service that has
never published at all. The dashboard discovers services by
`SCAN MATCH kpidash:services:*:*`; no key means no card, which is
indistinguishable from a service that was never expected.

That gap is **out of scope by decision, not by oversight**. Closing it needs
the dashboard to know what it *should* be seeing, and this contract is
entirely discovery-based — so it would need a new declared-services key
(`kpidash:services:expected` or similar). That is a contract addition binding
on both consumers (kpidash and kdeskdash), and it buys the case that has never
bitten: a service that has never published has never been trusted, and the
failure that prompted WI #902 was stale-GREEN, which is fixed. **A publisher
that is expected to be watched must publish at least once.** If a future
consumer needs the declared-services key, that is a contract change to propose
on its own evidence, not a bug in this one.

CLI example:

```bash
# Per-host service
uv run kpidash-client service-status --name api --state ok --text "API responding" --host kubs0

# Non-host-scoped service (writes to kpidash:services:api:_, host line hidden)
uv run kpidash-client service-status --name api --state ok --text "API responding"
```

---

## 8b. Apt-Temps Cards (Sprint 012, WI #364)

| Key | Type | Written by | Read by | TTL |
|-----|------|-----------|---------|-----|
| `kpidash:apttemps:{zone}` | STRING (JSON) | apt-temps publisher | dashboard | **none** |

One key per zone → one card per zone. `{zone}` is a lowercase slug (no spaces),
e.g. `living`, `kitchen`, `bedroom`.

```json
{ "zone": "Living", "temp_f": 75.6, "humidity_pct": 58, "ts": 1743292800.0 }
```

| Field | Type | Required | Notes |
|-------|------|----------|-------|
| `zone` | string | no | Display label (proper case); the **key** slug is authoritative |
| `temp_f` | number | yes | Temperature °F, rendered to 1 decimal |
| `humidity_pct` | number | yes | Relative humidity, whole number |
| `ts` | number | yes | Unix seconds; drives staleness |

TTL-less — the dashboard owns freshness via `ts` (mirrors the services-card
idiom). Dashboard polls `SCAN MATCH kpidash:apttemps:*` and renders one card per
key (sorted by slug) in the footer strip, **to the right of the service cards**.
Temperature colour: Blue `<65.0` · Green `65.0–75.0` · Orange `75.1–79.9` ·
Red `>79.9`; the card goes GRAY when `(now − ts) ≥ 300 s`. Keys are published by
the separate **apt-temps** project; the dashboard only reads them. Delete a
key to remove its card.

---

## 8d. Network-Health Card (Sprint 024, WI #4061 / #4062)

| Key | Type | Written by | Read by | TTL |
|-----|------|-----------|---------|-----|
| `kdash:net:{host}` | STRING (JSON) | kpidash-client (Linux), kpidash-client-win (cleo) | dashboard | 15 s |

**The contract is kdashdata's, not this file's**: schema
`lib/kdashdata/contracts/schemas/kdash-net.schema.json`, rulings in CD-27 of
`lib/kdashdata/docs/architecture.md`. Field names are pinned; a breaking change
lands as `kdash:net:v2:{host}`. Both ends are gated against that schema:
`clients/kpidash-client/tests/test_net.py` validates every record shape the
producer builds with kdashdata's own validator, and `tests/test_netstate.c`
requires the reader to accept every schema example and refuse every
counterexample.

**Producer** (`kpidash_client/net.py`, Linux only, on by default there): every
telemetry interval (~5 s), `SET … EX 15`, from its own daemon thread. Each host
probes its own path — one ICMP echo each to the UDM (`192.168.1.1`), every
fabric peer's bond address except its own, and an Internet target
(`1.1.1.1`), concurrently under a 2.5 s deadline. `[net]` in config.toml
overrides any of it:

```toml
[net]
enabled = true                      # default: true on Linux, false elsewhere
bond    = "bond0"
udm     = "192.168.1.1"
wan     = "1.1.1.1"
peers   = ["192.168.1.235", "192.168.1.60", "192.168.1.111", "192.168.1.200"]
nic     = "AQC113"                  # display label; detected from the PCI id if omitted
```

`lfc_delta_24h`, `rx_errors_delta_24h` and `drops` need memory across
restarts, so the producer keeps `net-history.json` under its state dir
(`$STATE_DIRECTORY`, else `$XDG_STATE_HOME/kpidash-client`, else
`~/.local/state/kpidash-client`): 24 h of counter samples, one kept per five
minutes, and the last hour of drop events. A restart therefore never reports
`(+0)` for a window it did not see; a delta is `null` only before the first
window exists. `kpidash-client net` prints one record without touching Redis
or that file.

**Reader**: one `MGET` per second for the hosts in `KPIDASH_NET_HOSTS`
(default `kubs0,kubsdb,kai,gratch,cleo`, card order, first five drawn). Same
freshness convention as the host cards: a key that is absent this poll means
the publisher is gone, and the column keeps the last record and renders it
hatched `STALE · n s` — never healthy. A host that has **never** published is
**no feed** (grey, hatched, not a fault: its client is not installed yet). A
record that fails the contract is treated as absent.

"Has published" outlives the process (WI #4085): a host's first live record
admits it to the `[net]` section of the admitted-hosts file (§1). After a
restart, an admitted host with no key reads `STALE · no record yet` and counts
in the headline like any other stale column — so a producer that died *before*
the restart no longer reads calmer than one that died after. A host in
`KPIDASH_NET_HOSTS` that was never admitted stays no feed. Admission is
append-only; a host dropped from the list keeps its line.

`reach.wan` is debounced by the reader, not the producer (sprint 035, WI
#4542): each producer sends one echo per record, so the panel draws an
Internet failure only once **3 consecutive records** from that host have
`wan: false` (≈ 10–15 s), counting each record once by its `ts`, and one good
record clears it. A record whose `udm` probe also failed is drawn at once.
`udm` and `peers` are not debounced. The contract is unchanged: producers
still report each probe as it came out.

The panel derives the verdict,
uplink-lost (live hosts reach peers but none reaches the UDM), WAN-down, the
header RTT tiles (medians) and a 60-minute, per-minute throughput history.

```bash
# What the card reads for one host (rpi53 Redis needs auth)
redis-cli -h rpi53 GET kdash:net:kai | jq .
```

### Clearing a reviewed warning (WI #4143)

| Key | Type | Written by | Read by | TTL |
|-----|------|-----------|---------|-----|
| `kdash:net:ack:{host}` | STRING (JSON) | `kpidash-client net ack`, or a plain `SET` | dashboard | 86400 s |

Contract: `lib/kdashdata/contracts/schemas/kdash-net-ack.schema.json`, the
CD-27 addendum. A yellow column clears two ways:

- **Time** — by construction: yellow comes from the producer's 24 h deltas
  and the last hour's `drops`, so a single event stays yellow for 24 h (the
  red drop tick for 1 h) and then clears on its own.
- **An ack** — the host's `lfc` and `rx_errors` at review time, plus `by` and
  `ts`. **The panel applies it**, not the producer: each effective delta is
  `min(<counter>_delta_24h, <counter> − ack.<counter>)` (the live counter
  itself if it fell below the baseline, i.e. reset), and only `drops` after
  `ack.ts` count. Anything that happens after the ack turns the column yellow
  again on the next tick. The 24 h EX matches the delta window, so an expired
  ack changes nothing.

An ack **never clears red** (stale, no link, no path) and never clears "10G
down · on 1G backup" — that is a condition still true now, not a past event,
and time does not clear it either. Acking a stale host is refused: there is no
live record to baseline.

```bash
# From any host with kpidash-client >= 1.4.0 and REDISCLI_AUTH
kpidash-client net ack kubsdb --by claude-kai

# The documented plain SET, for an agent with redis-cli on rpi53. Copy lfc and
# rx_errors from the host's live record (null stays null); ts is unix seconds.
redis-cli GET kdash:net:kubsdb | jq '{lfc, rx_errors}'
redis-cli SET kdash:net:ack:kubsdb '{"lfc":7,"rx_errors":0,"by":"claude-kai","ts":1793080000}' EX 86400
```

The card shows no "ack'd" marker: a cleared column looks like one that never
warned. `redis-cli GET kdash:net:ack:{host}` says whether, and by whom.

---

## 8c. Host Staleness Cards (Sprint 017, WI #1903)

| Key | Type | Written by | Read by | TTL |
|-----|------|-----------|---------|-----|
| `kdash:stale:{host}:{deployer}` | STRING (JSON) | the fleet deployers | dashboard | **none, ever** |

One key per **(host, deployer)** pair → one card per stale *host*, listing every
deployer that flagged it. Note the `kdash:` namespace: this feed is registered
in **kdashdata** (WI 1904, decision CD-18) and shared with other readers, so the
dashboard is a consumer here rather than the owner.

Current writers: `fleet-deploy` in **agent-skills**, and `apply` / `audit` in
**k-homelab**. Both reach Redis through `kdash-pub`, so a new deployer joining
the feed needs no change here.

```json
{ "stale": true, "since": 1788594159, "reason": "audit skipped: unreachable", "ts": 1788594159 }
```

| Field | Type | Required | Notes |
|-------|------|----------|-------|
| `stale` | bool | — | Pinned `const: true` by the schema. **The dashboard does not read it** — see below |
| `since` | number | for the detail | Unix **seconds**, a bare JSON number. When this deployer *first* skipped the host; set once and carried, never restamped |
| `reason` | string | no | One human-facing line. Not rendered today |
| `ts` | number | no | Unix seconds; moves on every write. Not rendered |

### The feed is presence-owned, and that inverts the usual rule

There is no TTL and no staleness window: **the key existing is the flag, and its
absence is the only all-clear.** A deployer that skips a host once and is never
run again correctly leaves the card up forever — that is the true state of the
world, and nothing garbage-collects it.

Everywhere else in this protocol the dashboard skips an unparseable record and
moves on. Here it must not, because skipping this one renders as *all-clear* — a
single parse bug would report a three-week outage as healthy. So every failure
path falls back to **"still stale, detail unknown"**:

| What went wrong | What the dashboard does |
|---|---|
| Payload absent, empty, corrupt, or not an object | Host still shown stale; `since` dropped |
| `since` is an ISO-8601 string instead of a number | Same — the record is rejected, the flag is not |
| `stale: false` published (off-contract) | Host still shown stale — the writer left the key up, so it cleared nothing |
| Key has no deployer segment, or too many | Host still shown stale; deployer listed as `(unknown)` |
| `SCAN` fails part-way through | **Whole cycle abandoned**; the previous cards stay up |
| Key deleted between the `SCAN` and the `GET` | Skipped — this is the one case where absence really is the all-clear |
| Key names no host at all (`kdash:stale:`) | Ignored; there is nothing to attribute the flag to |

**Clear a flag with `DEL` and nothing else.** Publishing `stale: false` leaves
the key standing and therefore leaves the card up.

### Render

Dashboard polls `SCAN MATCH kdash:stale:*` inside the same once-a-second cycle
that scans `kpidash:services:*`, and paints one card into the footer strip after
the service and apt-temps cards, same 220×240 footprint and warn-coloured band:

```
  komarchy stale
    k-homelab
    agent-skills
```

Body lists **deployer names, one per line, oldest `since` first** — so the
deployer that has been waiting longest is at the top. A deployer whose `since`
could not be read carries no ordering information and sorts last rather than
claiming to be the oldest; ties break by name so the card does not reshuffle
between polls. Hosts are sorted by name. The card disappears the second the
host's last key does.

**`rpi53` is excluded**: it is the panel's own host, and if it is behind there
is no dashboard to say so.

**Intermittent hosts wait out a week** (sprint 026, WI #4111, k-homelab
korg:4023 decision 2(i)). For a host in `KPIDASH_INTERMITTENT_HOSTS` — default
`komarchy,kbook2,kimac,thrall`, the `availability: intermittent` entries of
k-homelab's `inventory.yml` — the card stays hidden until the host's **oldest**
flag's `since` is **more than 7 days** old (`STALE_INTERMITTENT_GRACE_S`, the
same number as kmon's `KMON_ABSENCE_DAYS`). A closed laptop is not a call for
attention; one closed for over a week is. Every other host's card shows
immediately, as before.

The grace is the reader's alone. The keys stay exactly as truthful as before,
no writer changed, and the flag carries no availability — hence the config
list. An unreadable `since` cannot prove a flag is young, so it voids the grace
and the card shows (CD-18: unknown is stale). Set `KPIDASH_INTERMITTENT_HOSTS=`
(empty) to turn the grace off.

Limits: `STALE_REGISTRY_MAX` hosts (8) and `STALE_DEPLOYERS_MAX` deployers per
host (8); anything beyond is dropped rather than overflowing the card.

```bash
# What the dashboard sees (rpi53 Redis needs auth)
redis-cli --scan --pattern 'kdash:stale:*'

# Deployers raise and clear their own flags — do not hand-write these.
# The writers are the point: a hand-made key proves only that the card parses JSON.
```

---

## 8e. Update-Issue Cards (Sprint 028, WI #3642)

| Key | Type | Written by | Read by | TTL |
|-----|------|-----------|---------|-----|
| `kdash:update:{host}:{kind}` | STRING (JSON) | k-homelab's apt runner, through `kdash-pub` | dashboard | **none, ever** |

Registered in **kdashdata** (sprint 020, schema `contracts/schemas/kdash-update.schema.json`);
the dashboard is a consumer. One key per **(host, kind)** — a held package and a pending
reboot on one host coexist and clear independently — and one card per *host*.

```json
{ "since": 1793581200.0, "ts": 1793667600.4,
  "issue": "rpi53: reboot needed for linux-image 6.12.48 (apt run 2026-11-02)",
  "ref": "korg:3627" }
```

| Field | Type | Required | Notes |
|-------|------|----------|-------|
| `since` | number | yes | Unix **seconds**. When the issue was *first* raised; carried, never restamped. Orders the card body |
| `ts` | number | yes | Unix seconds of the latest run that found it. Not rendered |
| `issue` | string | yes | ≤200 chars. **The line the card renders**, verbatim |
| `ref` | string | no | `korg:<id>`. Not rendered today |

`{kind}` is an **open vocabulary** — `ring-stopped`, `reboot-awaiting`, `held`,
`gate-failed` today. The card renders the `issue` line, not the kind, so a kind
this build has never heard of shows exactly like a known one.

**Presence-owned, exactly as §8c.** The key is the flag, `DEL` is the only
all-clear, and every failure path falls back to *flagged, detail unknown*
(kdashdata CD-18): an unreadable or absent payload, or a missing `issue`,
renders `<kind>: detail unknown`; a bad `since` only loses the ordering; a key
with a missing or extra segment shows kind `(unknown)`; a `SCAN` that fails
part-way abandons the cycle and leaves the previous cards up.

### Render

`SCAN MATCH kdash:update:*` in the same once-a-second cycle as §8c. One card per
host in the footer strip, **after the staleness cards and before apt-temps**,
same 220×240 footprint and warn band:

```
      rpi53 update
  rpi53: reboot needed for
  linux-image 6.12.48 (apt
     run 2026-11-02)
```

Body: one `issue` line per kind, **oldest `since` first** (unknown `since` last,
ties by kind), set at 16 px and wrapped. There is no input to scroll with, so a
body longer than the card is clipped at the bottom — the oldest issue is the one
kept in view. Hosts sorted by name.

**`rpi53` is included** — unlike §8c. "The panel needs a reboot" is exactly
what this card is for, and rpi53 is in the Pi update ring.

Limits: `UPDATE_REGISTRY_MAX` hosts (8), `UPDATE_KINDS_MAX` issues per host (6).

```bash
redis-cli --scan --pattern 'kdash:update:*'
# The apt runner raises and clears these. A hand-written key proves the card
# renders, not that the feed works — delete it afterwards.
```

---

## 8f. NAS Big Panel (Sprint 028, WI #3956)

| Key | Type | Written by | Read by | TTL |
|-----|------|-----------|---------|-----|
| `kdash:nas:{host}` | STRING (JSON) | kpidash-client on the NAS host (gratch) | dashboard | 15 s |

**The contract is kdashdata's** (sprint 020): schema
`lib/kdashdata/contracts/schemas/kdash-nas.schema.json`, field mapping from the
source in `contracts/registry.md`. **v1, field names pinned**; a breaking change
is `kdash:nas:v2:{host}`. Both ends are gated against the schema:
`tests/test_nas.py` validates the projection of a real gratch capture with
kdashdata's validator, and `tests/test_nasstate.c` parses the schema's examples.

**Producer** (`kpidash_client/nas.py`, 1.5.0): every telemetry interval (~5 s),
`SET … EX 15`. It re-reads gratch-ui's root collector output,
`/run/gratch-ui/privileged.json`, and **never runs `smartctl`** — gratch-ui is the
one place that knows how to leave a standby IronWolf asleep. Values the source
does not give for a disk type are present-and-null. A missing or unreadable
source still publishes `collected_ts: null, source_stale: true` with empty
arrays, so the panel says *collector down* instead of going blank. On by default
in **auto** mode: it publishes only once the source file has existed on the
host, so no other host writes the key.

```toml
[nas]
enabled = true                              # absent = auto; false = off
source  = "/run/gratch-ui/privileged.json"
```

**Panel** (`src/nasstate.c` model, `src/widgets/nas_card.c`): one `GET
kdash:nas:gratch` per poll. The host is the constant `KPIDASH_NAS_HOST`.
The panel has a fixed slot in row 1, columns 2-3, next to the 10G card
(sprint 032). It is hidden until the key has published this run, and its slot
stays reserved either way, so the 10G card never moves.

Since sprint 033 the card is "A · Rail" in mauve (design:
`sprints/planning/nas-card-mockups.html`, function `vA`), the 10G card's
skeleton in the NAS's own colour. Nothing on it is under 20 px.

- **Headline.** `NAS OK`, or the first thing in trouble: a pool
  (`nas DEGRADED`, `nas 3 ERRORS`), then the worst disk (`sdc PENDING 2`,
  `sdd SMART FAILED`), then the collector (`COLLECTOR DOWN`, `SOURCE STALE`).
  The line under it counts disks (or the ones that need a look), gives the
  first pool's last scrub and, when all is well, its error count. Three stat
  boxes: pool %, hottest disk, data age (the collector's clock).
- **Bars.** The first pool's usable used over usable total (after parity,
  never `alloc_bytes`), red when the pool is not ONLINE, and the root
  filesystem from gratch's own `kpidash:client:gratch:telemetry` disk labelled
  `root`. A second pool would count toward the headline and frame but gets no
  bar; gratch has one.
- **Disk tiles.** Eight, in Ken's order — the root NVMe, the three pool
  NVMes, the cache SSD, the three cold HDDs — each with its use above it
  (`nas pool` in peach), device, kind, temperature with a bar, power state,
  one extra fact (pending, media errors, wear, reallocated) and a SMART pill.
  The feed carries no use or pool membership, so the dashboard supplies both
  from `src/nas_disk_uses.h`, matched on the exact model string; a disk it
  does not list gets no label and sorts last. Cold tiles have a pre-mixed
  blue-tinted fill.
- **Levels.** Red: a pool that is neither ONLINE nor DEGRADED, SMART failed,
  NVMe wear ≥100 %, or HDD/SSD ≥60 °C (NVMe ≥80 °C). Yellow: DEGRADED, pool or
  scrub errors, pending sectors or media errors, wear ≥80 %, HDD/SSD ≥50 °C
  (NVMe ≥70 °C), or a stale or missing collector. Grey (neutral): **standby**,
  which is normal for an IronWolf, and a constant reallocated count. With no
  history there is no rise to warn about.
- **Frame and colour.** Mauve is healthy; standby is grey; yellow and red
  keep their meaning. The frame is a thin neutral line and turns yellow or red
  (3 px) only when the overall level is WARN or CRIT. It is never green.
- **Freshness, as on the host cards.** If the key is gone, or its `ts` is more
  than 15 s old, the headline reads `STALE · age` in red and the frame, rail,
  tiles and pills go grey, with the last values kept in view. A stale feed
  never looks healthy.

---

## 8g. Weather Filler (Sprint 034, WI #4576)

| Key | Type | Written by | Read by | TTL |
|-----|------|-----------|---------|-----|
| `kdash:weather:home` | STRING (JSON) | kweather on kai, ~every 10 min | dashboard | none |

**The contract is kdashdata's** (sprint 021): schema
`lib/kdashdata/contracts/schemas/kdash-weather.schema.json`, notes in
`contracts/registry.md`. **v1, field names pinned**; a breaking change is
`kdash:weather:v2:{loc}`. `home` is a location token, not a host.
`tests/test_wxstate.c` requires every schema example to parse and every
x-counterexample to be distrusted, and uses the first live record
(`tests/fixtures/weather-home-2026-11-12.json`) for the rest.

**The dashboard only reads it.** kweather owns the key; never hand-write it to
test the card. `tests/render_weather` draws any state at any width offline.

**Panel** (`src/wxstate.c` model, `src/widgets/weather_card.c`): one `GET`
per poll; an unchanged value costs a strcmp, not a parse.

- **Freshness is the reader's.** No TTL, so the last record outlives a dead
  collector. A `ts` more than 1 h old is stale: the card goes grey and the age
  reads `STALE · 2 h` in red. An absent key keeps the last record.
- **Trust.** A record the schema would reject — `cond` outside the closed
  set, a millisecond `ts` or sun event, a percent outside 0..100, a
  temperature outside ±150, a required field absent rather than null, more
  than 8 days — is logged once and ignored; the previous record stays.
- **Nulls draw a dash**; `now.cond` null is no icon and "No current
  reading / forecast only". Sun times are formatted in the panel's own
  timezone (rpi53: America/Los_Angeles).
- **Never seen** this run: not drawn, like the NAS card before its feed.

```sh
# What the card reads (rpi53 Redis needs auth) — read only
redis-cli -h rpi53 GET kdash:weather:home
```

---

## 8h. Claude Limits Card (Sprints 037 and 039, WI #4609, #4631)

| Key | Type | Written by | Read by | TTL |
|-----|------|-----------|---------|-----|
| `claude:limits` | HASH | Claude Code statusline and oauth poll, every host running it (kdeskdash `publisher/claude-pub.sh`) | dashboard, kdeskdash | none |

**The contract is kdashdata's**: schema
`lib/kdashdata/contracts/schemas/claude-limits.schema.json`, notes in
`contracts/registry.md`. It is the `KDASH_CLAUDE_REDIS` family, which answers
`rpi53:6379` — the same Redis the dashboard already reads, so the card uses
the existing connection. **If that stem ever moves off rpi53, the card needs
a second connection**: kpidash links `kdash_core` only, not libkdash's
stem-resolving I/O shell.

**The dashboard only reads it.** Never hand-write a fake record to the live
key; `tests/render_limits` draws ok / warn / stale / bare / live037 and, for a
record with no five-hour window, nofive-ok / nofive-warn / nofive-stale /
nofive-long / seven / live offline.

**Panel** (`src/limstate.c` model, `src/widgets/limits_card.c`): one
`HGETALL` per poll, parsed by libkdash (`kdash_parse_claude_limits`).

- **Freshness is per gauge**: headline and scoped sets carry independent
  stamps and cadences, judged by libkdash at a 60 s grace.
- **Absent key, rejected record:** the last record stays. A write with no
  scoped set keeps the last scoped set (greying by its own stamp).
- **Never seen** this run: not drawn.
- **`five_hour_none 1`** (CD-29) draws 7 DAY and the scoped window as the two
  rings (7 DAY alone with no scoped set); absent or `0` draws the 5 HOUR /
  7 DAY layout. The panel switches only after three consecutive new, fresh
  records agree (the first record sets it), so a mix of writers with and
  without the flag does not flap it.

```sh
# What the card reads (rpi53 Redis needs auth) — read only
redis-cli -h rpi53 HGETALL claude:limits
```

---

## 8i. korg + klams Card (Sprint 038, WI #4613)

| Key | Type | Written by | Read by | TTL |
|-----|------|-----------|---------|-----|
| `kdash:korg:pulse` | STRING (JSON) | korg's `bin/korg-pulse` timer on kai, every 5 min | dashboard | none |
| `kdash:klams:status` | STRING (JSON) | klams-monitor on kubs0, every 60 s | dashboard | none |

**The contracts are kdashdata's** (sprint 023, CD-28): schemas
`lib/kdashdata/contracts/schemas/kdash-korg-pulse.schema.json` and
`kdash-klams-status.schema.json`. Both are ts-owned and cadence-declared:
each record carries its writer's `expected_refresh_s`, and the panel judges
it stale past that plus `KDASH_CADENCE_GRACE_S` (60 s). Null counts read as
`KDASH_COUNT_NULL`, null stamps as 0; over-long arrays are truncated and
flagged, not rejected.

**The dashboard only reads them.** Never hand-write a fake record to a live
key; `tests/render_korg_klams` draws ok / warn / stale / down / nofeed / live
offline.

**Panel** (`src/kkstate.c` model, `src/widgets/korg_klams_card.c`): one `GET`
of each per poll, parsed by libkdash (`kdash_parse_korg_pulse`,
`kdash_parse_klams_status`). kpidash links `kdash_core` only, so it does its
own GET rather than libkdash's `kdash_korg_pulse()`.

- **Each half is stale on its own feed.** A fresh klams record with health
  `down` means klams is down; a stale one means its publisher is.
- **Scanner rows are judged by `last_ran_at`**, not the write: see
  ARCHITECTURE.md's card section for the three states.
- **Absent key, rejected record:** the last record stays. A subsystem miss or
  a non-ok verdict shows only on two consecutive records.
- **Never seen** this run: the half draws its title and "no feed".

```sh
# What the card reads (rpi53 Redis needs auth) — read only
redis-cli -h rpi53 GET kdash:korg:pulse
redis-cli -h rpi53 GET kdash:klams:status
```

---

## 9. Dev Commands (Sprint 002)

Command keys enable/disable developer overlay widgets. All keys use JSON
values with a TTL of 300 s (auto-expire).

### 9.1 Grid Overlay

| Key | Type | Written by | Read by | TTL |
|-----|------|-----------|---------|-----|
| `kpidash:cmd:grid` | STRING (JSON) | manual (redis-cli) | dashboard | 300 s |

**Pixel-based mode** (original):
```json
{"enabled": true, "size": 50}
```

**Unit-based mode** (Sprint 003):
```json
{"enabled": true, "unit": true, "size": 1}
```

| Field | Type | Notes |
|-------|------|-------|
| `enabled` | bool | Show/hide grid overlay |
| `size` | int/float | Pixel spacing (pixel mode) or unit multiplier (unit mode) |
| `unit` | bool | Optional. When `true`, `size` is a unit multiplier (0.5, 1, 2) and grid lines are drawn at interior cell boundaries using `layout.h` constants. When absent or `false`, pixel-based mode (unchanged from Sprint 002). |

```bash
# Pixel grid (50px spacing)
redis-cli SET kpidash:cmd:grid '{"enabled":true,"size":50}' EX 300

# Unit grid (1-unit intervals — lines at cell boundaries)
redis-cli SET kpidash:cmd:grid '{"enabled":true,"unit":true,"size":1}' EX 300

# Half-unit intervals
redis-cli SET kpidash:cmd:grid '{"enabled":true,"unit":true,"size":0.5}' EX 300

# Double-unit intervals (2×1 widget boundaries)
redis-cli SET kpidash:cmd:grid '{"enabled":true,"unit":true,"size":2}' EX 300
```

### 9.2 Text Size Reference

| Key | Type | Written by | Read by | TTL |
|-----|------|-----------|---------|-----|
| `kpidash:cmd:textsize` | STRING (JSON) | manual (redis-cli) | dashboard | 300 s |

```json
{"enabled": true}
```

Shows sample text at various font sizes for visual calibration.

### 9.3 Dev Graph and 9.4 Fortune Dev Overlay (removed)

`kpidash:cmd:graph`, `kpidash:cmd:graph:{host}` and `kpidash:cmd:fortune_dev`
toggled the per-host dev graphs and the Fortune widget's calibration overlay.
Both widgets were removed in sprint 032, and the dashboard no longer reads the
keys. The section numbers are kept so references to §9.5 stay valid.

### 9.5 Device Screenshot (Sprint 011)

| Key | Type | Written by | Read by | TTL |
|-----|------|-----------|---------|-----|
| `kpidash:screenshot` | STRING | manual (redis-cli) / `krpidss` | dashboard | none (consumed) |

One-shot device self-screenshot. The dashboard consumes the key with `GETDEL`
on its next 1 s poll, renders the active screen with `lv_snapshot_take`, and
writes a 24-bit BMP. A value starting with `/` names the output path; anything
else uses the default `/tmp/kpidash-shot.bmp`. Because rendering is DRM/KMS
there is no external framebuffer grab — the shot is taken in-process.

```bash
# Trigger a shot to the default path (rpi53 Redis needs auth)
redis-cli SET kpidash:screenshot 1
```

Prefer the client CLI, which triggers the shot, waits for a fresh BMP, streams
it back over SSH, and converts to PNG:

```bash
krpidss [basename]        # -> ./kpidash-YYYYmmdd-HHMMSS.png (or ./basename.png)
```

`krpidss` lives in `scripts/` and installs to `~/.local/bin`. Host override via
`KRP_HOST` (default `ken@rpi53`). It prepends `scripts/kpidash-auth.sh` to the
script it runs on the Pi, so `REDISCLI_AUTH` is resolved **there**, from the
Pi's own `/etc/khomelab/secrets.env` — not carried across from this host.

### 9.6 Service-Card Evict (Sprint 013, WI #374)

| Key | Type | Written by | Read by | TTL |
|-----|------|-----------|---------|-----|
| `kpidash:cmd:services:evict` | STRING (JSON array) | `kpidash-cards` / manual; apt-temps (automatic, `kind: apttemps` only) | dashboard | `EX 60` by convention (consumed) |

Drops one or more cards from the dashboard's **in-memory** registries without a
restart. The dashboard consumes the key with `GETDEL` each 1 s poll and, on the
LVGL thread, destroys each matching card and removes it from `g_services[]` /
`g_apttemps[]`. This exists because the service/apt-temps registries do **not**
auto-evict — a `DEL`eted key's card otherwise lingers until restart.

Value is a JSON array of targets:

```json
[
  {"kind": "service",  "name": "ufw",    "host": "kubs0"},
  {"kind": "apttemps", "name": "office", "host": ""}
]
```

`kind` defaults to `"service"` if absent; for `apttemps`, `name` is the zone
slug and `host` is ignored. Pruning a card is **two steps** — `DEL` the data key
*and* publish this evict command:

```bash
redis-cli DEL kpidash:services:ufw:kubs0
redis-cli SET kpidash:cmd:services:evict '[{"kind":"service","name":"ufw","host":"kubs0"}]' EX 60
```

The `scripts/kpidash-cards` helper does both (list cards with age; prune
interactively or via `--service NAME[:HOST]` / `--zone SLUG`). **Service**
eviction is manual by design — a stopped service may stay as a reminder.

**apt-temps evicts its own zones automatically** (apt-temps sprint 007): when
its reconcile drops a zone it deletes the zone key and `SET`s this command — a
JSON list of `{"kind":"apttemps",…}` targets, `EX 60`, at most one command per
reconcile cycle. The key is shared with `kpidash-cards`, and each writer
replaces the whole value: if both write inside the same 1 s poll, the earlier
command is lost and its card lingers until a re-run or a restart. That race is
rare and is **noted, not guarded**.

---

## 10. System Info

| Key | Type | Written by | Read by | TTL |
|-----|------|-----------|---------|-----|
| `kpidash:system:logpath` | STRING | dashboard (start) | clients | none |
| `kpidash:system:version` | STRING | dashboard (start) | clients | none |
| `kpidash:system:mem:current` | STRING (JSON) | dashboard | diagnostic | none |
| `kpidash:system:mem:ring`    | LIST (JSON)   | dashboard | diagnostic | none |

`logpath` and `version` are plain strings (not JSON) written once on
dashboard startup.

The `mem:current` and `mem:ring` keys are diagnostic memory-telemetry
output written every 60 s by `src/memstat.c` (spec 005).
`mem:current` holds the latest sample; `mem:ring` is an `LPUSH`
ring trimmed to 1500 entries (~25 h). The JSON shape is:

```json
{
  "t": 1737088200,
  "uptime_s": 3600,
  "rss_bytes": 71303168,
  "vsize_bytes": 134217728,
  "lvgl_total": 1048576,
  "lvgl_free": 524288,
  "lvgl_used": 524288,
  "lvgl_max_used": 786432,
  "lvgl_frag_pct": 12,
  "lvgl_free_biggest": 262144
}
```

These keys are optional for clients — provided for soak debugging,
not part of the supported public API.

---

## Python Client Quick Reference

```python
# Minimal health ping loop
import redis, json, socket, time

r = redis.Redis(host="192.168.1.50", password="secret", decode_responses=True)
hostname = socket.gethostname().lower()
r.sadd("kpidash:clients", hostname)

while True:
    r.set(f"kpidash:client:{hostname}:health",
          json.dumps({"hostname": hostname, "last_seen_ts": time.time()}),
          ex=5)
    time.sleep(3)
```

Use `kpidash-client` for production use — it handles all keys, reconnection,
and cross-platform telemetry collection.
