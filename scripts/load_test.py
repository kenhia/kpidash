#!/usr/bin/env python3
"""
scripts/load_test.py — 8-client concurrent health + telemetry stress test (T062a)

SC-006 pass criterion: dashboard renders all 8 cards with no visible freeze
or flicker when this script runs alongside kpidash on the Pi 5.

Usage:
    # Set Redis credentials first:
    export REDISCLI_AUTH=yourpassword
    export KPIDASH_REDIS_HOST=192.168.1.50   # Pi 5 IP (or omit for localhost)

    python3 scripts/load_test.py [--clients 8] [--duration 30]

The script spawns N threads, each acting as a synthetic client.
Each thread writes health (EX 5) and telemetry (EX 15) at the same
interval as the real kpidash-client daemon.
"""
from __future__ import annotations

import argparse
import json
import os
import random
import socket
import string
import sys
import threading
import time

try:
    import redis
except ImportError:
    print("ERROR: redis-py not installed. Run: pip install redis", file=sys.stderr)
    sys.exit(1)

# ---------------------------------------------------------------------------
# Constants matching protocol.h
# ---------------------------------------------------------------------------
HEALTH_TTL_S     = 5
TELEMETRY_TTL_S  = 15
HEALTH_INTERVAL  = 3   # seconds
TELEMETRY_INTERVAL = 5  # seconds

# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------

def _connect() -> redis.Redis:
    host = os.environ.get("KPIDASH_REDIS_HOST", "127.0.0.1")
    port = int(os.environ.get("KPIDASH_REDIS_PORT", "6379"))
    auth = os.environ.get("REDISCLI_AUTH")
    r = redis.Redis(
        host=host,
        port=port,
        password=auth,
        decode_responses=True,
        socket_connect_timeout=5,
        socket_timeout=10,
    )
    r.ping()
    return r


def _fake_hostname(index: int) -> str:
    suffix = "".join(random.choices(string.ascii_lowercase, k=4))
    return f"loadtest-{index:02d}-{suffix}"


def _write_health(r: redis.Redis, hostname: str) -> None:
    payload = json.dumps({
        "hostname": hostname,
        "last_seen_ts": time.time(),
        "uptime_seconds": float(random.randint(0, 1_000_000)),
    })
    r.set(f"kpidash:client:{hostname}:health", payload, ex=HEALTH_TTL_S)
    r.sadd("kpidash:clients", hostname)


def _write_telemetry(r: redis.Redis, hostname: str) -> None:
    payload = json.dumps({
        "hostname": hostname,
        "ts": time.time(),
        "cpu_pct": round(random.uniform(5.0, 95.0), 1),
        "top_core_pct": round(random.uniform(10.0, 100.0), 1),
        "ram_used_mb": random.randint(512, 28672),
        "ram_total_mb": 32768,
        "gpu": {
            "name": f"GPU-{hostname}",
            "vram_used_mb": random.randint(256, 8192),
            "vram_total_mb": 8192,
            "compute_pct": round(random.uniform(0.0, 100.0), 1),
        },
        "disks": [
            {
                "label": "root",
                "type": "ssd",
                "used_gb": round(random.uniform(10.0, 900.0), 1),
                "total_gb": 953.9,
                "pct": round(random.uniform(1.0, 95.0), 1),
            }
        ],
    })
    r.set(f"kpidash:client:{hostname}:telemetry", payload, ex=TELEMETRY_TTL_S)


# ---------------------------------------------------------------------------
# Per-client worker thread
# ---------------------------------------------------------------------------

class ClientWorker(threading.Thread):
    def __init__(self, index: int, stop_event: threading.Event,
                 stats: dict) -> None:
        super().__init__(daemon=True, name=f"client-{index:02d}")
        self.index = index
        self.stop = stop_event
        self.stats = stats
        self.hostname: str = ""

    def run(self) -> None:
        try:
            r = _connect()
        except redis.RedisError as e:
            print(f"[{self.name}] Cannot connect to Redis: {e}", file=sys.stderr)
            self.stats["errors"] += 1
            return

        self.hostname = _fake_hostname(self.index)
        next_health = time.monotonic()
        next_telemetry = time.monotonic()
        writes = 0

        print(f"[{self.name}] starting as '{self.hostname}'")

        while not self.stop.is_set():
            now = time.monotonic()
            try:
                if now >= next_health:
                    _write_health(r, self.hostname)
                    next_health = now + HEALTH_INTERVAL
                    writes += 1
                if now >= next_telemetry:
                    _write_telemetry(r, self.hostname)
                    next_telemetry = now + TELEMETRY_INTERVAL
                    writes += 1
            except redis.RedisError as e:
                print(f"[{self.name}] Redis error: {e}", file=sys.stderr)
                self.stats["errors"] += 1
                time.sleep(1)
                continue

            self.stats["writes"] += 1
            time.sleep(0.5)

        # Cleanup: remove synthetic entries
        try:
            r.srem("kpidash:clients", self.hostname)
            r.delete(f"kpidash:client:{self.hostname}:health")
            r.delete(f"kpidash:client:{self.hostname}:telemetry")
        except redis.RedisError:
            pass
        print(f"[{self.name}] stopping after {writes} writes")


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------

def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--clients", type=int, default=8,
                        help="Number of synthetic clients (default: 8)")
    parser.add_argument("--duration", type=int, default=30,
                        help="Test duration in seconds (default: 30)")
    args = parser.parse_args()

    print(f"kpidash load test: {args.clients} clients for {args.duration}s")
    print(f"Redis: {os.environ.get('KPIDASH_REDIS_HOST', '127.0.0.1')}")
    print()

    # Verify connectivity
    try:
        r = _connect()
        print("Redis: connected OK")
        r.close()
    except Exception as e:
        print(f"ERROR: cannot connect to Redis: {e}", file=sys.stderr)
        sys.exit(1)

    stop_event = threading.Event()
    shared_stats: dict = {"writes": 0, "errors": 0}

    workers = [
        ClientWorker(i, stop_event, shared_stats)
        for i in range(args.clients)
    ]
    for w in workers:
        w.start()

    start = time.monotonic()
    try:
        while time.monotonic() - start < args.duration:
            elapsed = time.monotonic() - start
            print(f"\r  elapsed: {elapsed:.0f}s / {args.duration}s  "
                  f"writes: {shared_stats['writes']}  "
                  f"errors: {shared_stats['errors']}  ",
                  end="", flush=True)
            time.sleep(1)
    except KeyboardInterrupt:
        print("\nInterrupted.")

    print()
    stop_event.set()
    for w in workers:
        w.join(timeout=5)

    print()
    print("=" * 60)
    print(f"Total writes: {shared_stats['writes']}")
    print(f"Total errors: {shared_stats['errors']}")
    duration_actual = time.monotonic() - start
    rate = shared_stats["writes"] / max(duration_actual, 1)
    print(f"Write rate:   {rate:.1f} writes/s over {duration_actual:.1f}s")
    print()

    if shared_stats["errors"] > 0:
        print("RESULT: FAIL — Redis errors occurred during test")
        sys.exit(1)
    else:
        print("RESULT: PASS — all writes succeeded (SC-006 Redis layer)")
        print()
        print("SC-006 render verification requires visual inspection:")
        print("  • All 8 client cards should appear on the dashboard")
        print("  • No visible freeze or flicker during the test")
        print("  • Health LEDs should be green throughout")


if __name__ == "__main__":
    main()
