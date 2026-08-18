# Sprint 016 — fleet-deploy the khlenv-wired kpidash-client

- **korg**: work item #1417, proposal 1421. Slice 2 of program 1420
  (khlenv rollout — kpidash publishers); slice 1 was sprint 015 / khlenv
  002 (proposal 1419), which shipped the wiring and proved it on kubs0.
- **Branch**: `016-fleet-deploy-khlenv-client`
- **Scope**: roll kpidash-client 1.1.0 out to **kai, kubsdb, rpi53** —
  the three Linux hosts still on 1.0.0 with a local `[redis] host`.
  cleo stays out (Windows client needs a native port of the cache
  convention; kpidashclient-win #1418).

## Narrative

**2026-08-18** — No new code was needed: 015 shipped a client that
already does the right thing, and `[redis] host` was deliberately left as
an explicit override so the fleet could migrate one host at a time. This
sprint is the migration, and the work is entirely the per-host install
variance the WI predicted — plus one thing it did not.

Reviewed 015 first: gate green (7 ctest + 80 pytest), `resolve_redis_endpoint`
called from `connect()` on every attempt, explicit-null treated as an
error rather than a fallback. Nothing to fix, so straight to deploying.

### The three hosts

- **kai** — `uv tool upgrade kpidash-client` (1.0.0 → 1.1.0, pulling
  `khlenv` 0.1.0 from the same store), `[redis]` dropped from
  `~/.config/kpidash-client/config.toml`, user unit restarted.
- **kubsdb** — identical path. **Its config path is confirmed as
  `~/.config/kpidash-client/config.toml`**, which retires the "kubsdb's
  exact config path unverified" flag that has been sitting in klams since
  the 2026-08-15 host-card diagnosis.
- **rpi53** — the source-tree host. Checkout rsync'd from kai (excluding
  `.venv` — kai is x86-64, rpi53 aarch64 — plus `build-*/` and the
  `lib/lvgl` submodule, which the client does not need), `uv sync` in
  `clients/kpidash-client`, `[redis]` dropped, system unit restarted.
  `install.sh` was not needed: the unit already points at the venv the
  2026-08-15 repair rebuilt.

Every host's previous config was kept as `config.toml.pre-khlenv`.

### The thing the WI did not predict: rpi53 had no tailnet DNS

rpi53 is a full Tailscale peer but was running with `CorpDNS: false`, so
it could not resolve **any** `*.encke-wahoo.ts.net` name. That breaks the
deploy twice over, and the second break is the non-obvious one:

- khlenv's compiled-in endpoint is the MagicDNS name
  `kubs0.encke-wahoo.ts.net:7770`, and khlenv binds **only** to kubs0's
  tailscale IP (`100.91.170.122:7770`) — so the LAN address is not a
  fallback, it is nothing at all.
- The homelab package index is `https://kubsdb.encke-wahoo.ts.net:4880`,
  so `uv sync` could not fetch the `khlenv` wheel either. Reaching it by
  IP would have broken TLS name verification.

Options were: enable MagicDNS, pin IPs in `/etc/hosts` +
`/etc/khlenv/endpoint`, or sideload wheels. Ken chose **enable MagicDNS** —
the root-cause fix, and the only one that leaves no pinned IP to rot.
`sudo tailscale set --accept-dns=true`; `/etc/resolv.conf` backed up to
`/etc/resolv.conf.pre-magicdns` first. Verified afterwards that tailnet,
public (`github.com`) and LAN names all still resolve. Rollback is
`sudo tailscale set --accept-dns=false` over ssh to `192.168.1.213`,
which needs no DNS to reach.

Worth stating plainly because it generalises: **a resolver whose own
address is a name is only as available as the name service.** khlenv's
D7 endpoint file exists precisely for hosts that cannot resolve the
default, and rpi53 was the fleet's one such host.

## Deployed

- **What**: kpidash-client **1.1.0** + khlenv 0.1.0 on kai, kubsdb, rpi53.
  kubs0 was already there from 015. All four Linux hosts now resolve
  `KPIDASH_REDIS` through khlenv with no `[redis]` in any config.
- **When**: 2026-08-18.
- **Verified live**:
  - All four host cards green — `kpidash:client:{kai,kubs0,kubsdb,rpi53}:health`
    refreshing on their TTLs, before and after every step.
  - **Server-side proof, per host.** khlenv's `/var/lib/khlenv/query.log`
    recorded a real resolve for each, with `host` derived by whois from
    the source address rather than self-declared:
    `{"host":"kai",...,"result":"hit","stem":"KPIDASH_REDIS"}` and the
    same for `kubsdb` and `rpi53`.
  - `~/.cache/khlenv/kpidash-client.json` written in the D6 shape on all
    three.
  - **Cache-as-failover drill (the WI's acceptance criterion), on kai.**
    Stopped `khlenv.service` on kubs0, confirmed the endpoint refused
    connections from kai, then restarted kai's client cold. It logged
    `khlenv: http://kubs0.encke-wahoo.ts.net:7770 unreachable (…Connection
    refused); using the cached value for KPIDASH_REDIS, last refreshed 3m
    ago.` and kept publishing throughout — health TTL never lapsed. khlenv
    restarted; all four cards green. **No card dropped at any point in the
    drill.**

## Follow-ups

- **`/etc/khlenv/endpoint` is absent on kai, kubsdb and rpi53**, so all
  three log khlenv's loud "no endpoint configured … Fix by having
  k-homelab render the endpoint file on this host" line on every start.
  It is only a warning — the compiled-in default is correct today — but
  the fix belongs to k-homelab, not here. Filed for k-homelab.
- rpi53's MagicDNS change is a machine-state change made outside the
  k-homelab recipes; recorded in klams.
