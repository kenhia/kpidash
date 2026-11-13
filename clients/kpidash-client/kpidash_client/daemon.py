"""daemon.py — kpidash-client background daemon (T024)

Runs concurrent health-ping and telemetry loops.
Health: every config.health_interval_s (default 3s)
Telemetry: every config.telemetry_interval_s (default 5s, A1)
khlenv retry: every KHLENV_RETRY_S while the endpoint came from a fallback
"""

from __future__ import annotations

import logging
import signal
import threading
import time

import psutil

from .config import ClientConfig
from .endpoint import khlenv_on_fallback
from .nas import NasCollector
from .net import NetCollector
from .redis_client import RedisClient, RedisClientError
from .telemetry.disk import collect_disks
from .telemetry.gpu import collect_gpu
from .telemetry.system import collect_os_name, collect_system

logger = logging.getLogger(__name__)

_stop_event = threading.Event()

#: How often to re-ask khlenv while running on a cached or default endpoint.
#: Each attempt is one GET with khlenv's 2 s timeout, and the loop stops
#: asking the moment an answer is live, so a short interval costs nothing.
KHLENV_RETRY_S = 60


def _uptime() -> float | None:
    try:
        return time.time() - psutil.boot_time()
    except Exception:
        return None


def _health_loop(rc: RedisClient, interval: int) -> None:
    os_name = collect_os_name()
    while not _stop_event.is_set():
        try:
            up = _uptime()
            rc.write_health(uptime_s=up, os_name=os_name)
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


def _dev_telemetry_loop(rc: RedisClient, interval: int) -> None:
    """Fast-poll GPU+CPU+RAM for dev graph (no disks)."""
    while not _stop_event.is_set():
        try:
            sys_data = collect_system()
            gpu_data = collect_gpu()
            payload = {**sys_data, "gpu": gpu_data}
            rc.write_dev_telemetry(payload)
        except RedisClientError as e:
            logger.warning("dev telemetry write failed: %s", e)
            rc.reconnect_on_failure()
        _stop_event.wait(interval)


def _net_loop(rc: RedisClient, config: ClientConfig) -> None:
    """kdash:net:{host} (WI #4061). Its own thread, so the probes — bounded,
    but up to a few seconds against a dead target — never delay telemetry."""
    try:
        collector = NetCollector(config.net)
        collector.prime()
    except Exception:
        logger.exception("network health disabled: collector failed to start")
        return
    while not _stop_event.wait(config.telemetry_interval_s):
        try:
            rc.write_net(collector.collect())
        except RedisClientError as e:
            logger.warning("net write failed: %s", e)
            rc.reconnect_on_failure()
        except Exception:
            logger.exception("net collect failed")


def _khlenv_loop(rc: RedisClient) -> None:
    """Self-heal a boot-time khlenv fallback without a restart (WI 4353)."""
    while not _stop_event.wait(KHLENV_RETRY_S):
        try:
            # khlenv logs each failed attempt itself; say only the recovery.
            if rc.refresh_endpoint_if_fallback() and not khlenv_on_fallback():
                logger.info("khlenv answered live again (endpoint %s)", rc.endpoint)
        except Exception:
            logger.exception("khlenv retry failed")


def _nas_loop(rc: RedisClient, config: ClientConfig) -> None:
    """kdash:nas:{host} (WI #3956). Re-reads gratch-ui's collector file each
    tick; on any host where that file never appears this publishes nothing."""
    collector = NasCollector(config.nas)
    while not _stop_event.is_set():
        try:
            record = collector.collect()
            if record is not None:
                rc.write_nas(record, config.telemetry_interval_s)
        except RedisClientError as e:
            logger.warning("nas write failed: %s", e)
            rc.reconnect_on_failure()
        except Exception:
            logger.exception("nas collect failed")
        _stop_event.wait(config.telemetry_interval_s)


def run_daemon(config: ClientConfig) -> None:
    """Block until SIGTERM/SIGINT or _stop_event is set."""
    _stop_event.clear()
    logger.info(
        "kpidash-client daemon starting (redis=%s)",
        f"{config.redis_host}:{config.redis_port or 6379} (config.toml)"
        if config.redis_host
        else "resolved through khlenv",
    )

    rc = RedisClient(config)
    try:
        rc.connect()
    except RedisClientError as e:
        logger.error("initial Redis connection failed: %s", e)
        # Continue anyway — loops will retry

    threads = [
        threading.Thread(target=_health_loop, args=(rc, config.health_interval_s), daemon=True),
        threading.Thread(target=_telemetry_loop, args=(rc, config), daemon=True),
    ]

    if config.nas.enabled is not False:
        threads.append(threading.Thread(target=_nas_loop, args=(rc, config), daemon=True))

    if not config.redis_host:
        threads.append(threading.Thread(target=_khlenv_loop, args=(rc,), daemon=True))

    if config.net.enabled:
        logger.info("network health loop enabled (kdash:net:%s)", rc.hostname)
        threads.append(threading.Thread(target=_net_loop, args=(rc, config), daemon=True))

    # Dev telemetry loop — only start if dev_interval_s is configured
    dev_interval = config.dev_interval_s
    if dev_interval is not None:
        logger.info("dev telemetry loop enabled at %ds interval", dev_interval)
        threads.append(
            threading.Thread(target=_dev_telemetry_loop, args=(rc, dev_interval), daemon=True)
        )
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
