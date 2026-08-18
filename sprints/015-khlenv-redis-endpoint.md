# Sprint 015 — resolve the Redis endpoint through khlenv

- **korg**: work item #1210 (khlenv project), proposal 1419. This is the
  kpidash half of a two-repo sprint; the khlenv half (the Python client
  itself) is khlenv sprint 002.
- **Branch**: `015-khlenv-redis-endpoint`
- **Scope**: the **kubs0** client only. Fleet deploy to kai / kubsdb /
  rpi53 is #1417; the Windows client (cleo) needs a native port of the
  cache convention and is #1418.

## Narrative

**2026-08-18** — kpidash-client is khlenv's first real consumer. The
motivating failure class is the one this repo already knows: the
2026-08-15 host-card faults were stale per-host config, and per-host
config is exactly what a resolver removes.

The wiring is small on purpose:

- New `kpidash_client/endpoint.py` resolves `(host, port)` from khlenv
  key **`KPIDASH_REDIS`** (value `"host:port"`, app `kpidash-client`).
  Endpoint only — the password stays in `REDISCLI_AUTH`, because khlenv
  never holds secrets.
- **`[redis] host` in `config.toml` is now optional and is an explicit
  override.** Present, it wins outright, port included; absent, khlenv
  resolves. That keeps every existing install on the fleet working
  untouched, which matters because #1417 will migrate them one at a time.
  `ClientConfig.redis_host` / `redis_port` are `None` when unset, and
  `test_missing_redis_host` became
  `test_absent_redis_host_means_resolve_through_khlenv`.
- **`RedisClient.connect()` resolves on every call**, not once at
  startup. This is the part that earns the sprint: `reconnect_on_failure()`
  already runs from every daemon loop on a write failure, so a Redis that
  moves is picked up within one loop interval without anyone touching a
  config file. `RedisClient.endpoint` reports what the last connect
  resolved.
- An **explicit null** from khlenv is an error, not a fallback — it means
  "intentionally no Redis endpoint", and connecting anyway would override
  a decision recorded deliberately in the store.

The khlenv client's own D6 rules do the rest: unreachable khlenv serves
the last cached answer with a loud age warning, and an uncached cold start
falls back to a compiled-in `rpi53:6379` — which is where the dashboard's
Redis has always been, so the wiring can never be worse than not having it.

`just check` now runs `check-dashboard` (the existing cmake/ctest gate)
and `check-client` (ruff + pytest) — the Python client had tests but no
gate before this. `clients/kpidash-client/pyproject.toml` declares the
homelab package index, since khlenv is published there rather than to
PyPI. 80 client tests green; the dashboard gate unchanged and green.

Store entry added on the k-homelab side:

```yaml
KPIDASH_REDIS: "rpi53:6379"
```

## Deployed

- **What**: `kpidash-client` **1.1.0**, published to the homelab package
  store (`kpkg add`), pulling `khlenv` 0.1.0 from the same store.
  `[redis]` removed from `~/.config/kpidash-client/config.toml` on kubs0
  (previous file kept as `config.toml.pre-khlenv`), so the endpoint is
  genuinely resolved rather than shadowed by a local override.
- **Where**: kubs0 only — `uv tool upgrade kpidash-client`, user unit
  `kpidash-client.service` restarted. kai / kubsdb / rpi53 still run
  1.0.0 with their own `[redis] host`, untouched and unaffected (#1417).
- **When**: 2026-08-18.
- **Verified live**:
  - khlenv's query log recorded the real resolve —
    `{"host":"kubs0","app":"kpidash-client","key":"KPIDASH_REDIS",`
    `"result":"hit","stem":"KPIDASH_REDIS"}` — with `host` derived by
    whois from the source address, not self-declared.
  - `~/.cache/khlenv/kpidash-client.json` written in the D6 shape.
  - `kpidash:client:kubs0:health` refreshing on its 5 s TTL; host card
    live.
  - **The propagation path, end to end and without a restart.** Added
    `KPIDASH_REDIS.kubs0: "127.0.0.1:6399"` to the store; the daemon took
    the moved address (`Cannot connect to Redis at 127.0.0.1:6399`) with
    no config edit anywhere, proving the store is the source of truth.
    Removing the stem again healed it **by itself** — the failing writes
    drove `reconnect_on_failure()`, the reconnect re-asked khlenv, and the
    card came back without the service being touched. Store restored;
    total outage on one host card, ~40 s.
  - Note on scope of that test: the break-then-heal direction is the one
    exercised live. Killing an established connection is *not* enough to
    trigger it — redis-py's pool reconnects transparently, so no
    application-level write failure occurs; the re-resolve fires when the
    address stops accepting connections, which is the real move scenario.
