"""daemon.py — kpidash-client background daemon (T024)

Runs concurrent health-ping and telemetry loops.
Health: every config.health_interval_s (default 3s)
Telemetry: every config.telemetry_interval_s (default 5s, A1)
Repo scan: every config.repo_scan_interval_s (default 30s)
"""

from __future__ import annotations

import logging
import signal
import threading
import time

import psutil

from .config import ClientConfig
from .redis_client import RedisClient, RedisClientError
from .repos import scan_repos
from .telemetry.disk import collect_disks
from .telemetry.gpu import collect_gpu
from .telemetry.system import collect_system

logger = logging.getLogger(__name__)

_stop_event = threading.Event()


def _uptime() -> float | None:
    try:
        return time.time() - psutil.boot_time()
    except Exception:
        return None


def _health_loop(rc: RedisClient, interval: int) -> None:
    while not _stop_event.is_set():
        try:
            up = _uptime()
            rc.write_health(uptime_s=up)
        except RedisClientError as e:
            logger.warning("health write failed: %s — reconnecting", e)
            rc.reconnect_on_failure()
        _stop_event.wait(interval)


def _telemetry_loop(rc: RedisClient, config: ClientConfig) -> None:
    while not _stop_event.is_set():
        try:
            sys_data = collect_system()
            gpu_data = collect_gpu()
            disk_data = collect_disks(config.disks)
            payload = {**sys_data, "gpu": gpu_data, "disks": disk_data}
            rc.write_telemetry(payload)
        except RedisClientError as e:
            logger.warning("telemetry write failed: %s", e)
            rc.reconnect_on_failure()
        _stop_event.wait(config.telemetry_interval_s)


def _repo_loop(rc: RedisClient, config: ClientConfig) -> None:
    while not _stop_event.is_set():
        try:
            repos = scan_repos(config.repos)
            rc.write_repos(repos)
        except Exception as e:
            logger.warning("repo scan failed: %s", e)
        _stop_event.wait(config.repo_scan_interval_s)


def run_daemon(config: ClientConfig) -> None:
    """Block until SIGTERM/SIGINT or _stop_event is set."""
    _stop_event.clear()
    logger.info("kpidash-client daemon starting (host=%s:%d)", config.redis_host, config.redis_port)

    rc = RedisClient(config)
    try:
        rc.connect()
    except RedisClientError as e:
        logger.error("initial Redis connection failed: %s", e)
        # Continue anyway — loops will retry

    threads = [
        threading.Thread(target=_health_loop, args=(rc, config.health_interval_s), daemon=True),
        threading.Thread(target=_telemetry_loop, args=(rc, config), daemon=True),
        threading.Thread(target=_repo_loop, args=(rc, config), daemon=True),
    ]
    for t in threads:
        t.start()

    def _shutdown(sig, frame):
        logger.info("kpidash-client shutting down (signal %d)", sig)
        _stop_event.set()

    signal.signal(signal.SIGTERM, _shutdown)
    signal.signal(signal.SIGINT, _shutdown)

    _stop_event.wait()
    for t in threads:
        t.join(timeout=5)

    rc.disconnect()
    logger.info("kpidash-client daemon stopped")
