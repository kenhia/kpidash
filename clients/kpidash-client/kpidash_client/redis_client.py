"""
redis_client.py — RedisClient wrapping redis.Redis (T012)

All Redis I/O for kpidash-client lives here. Password is read
exclusively from the REDISCLI_AUTH environment variable.
"""

from __future__ import annotations

import json
import os
import socket
import time
import uuid

import redis

from .config import ClientConfig

ACTIVITY_ZSET_TRIM = 20  # keep 2× display max in sorted set


class RedisClientError(Exception):
    """Raised when a Redis operation fails."""


class RedisClient:
    def __init__(self, config: ClientConfig) -> None:
        self._config = config
        self._r: redis.Redis | None = None
        self._hostname = config.hostname or socket.gethostname().split(".")[0].lower()

    # ---- Connection ----

    def connect(self) -> None:
        auth = os.environ.get("REDISCLI_AUTH")
        try:
            self._r = redis.Redis(
                host=self._config.redis_host,
                port=self._config.redis_port,
                password=auth,
                decode_responses=True,
                socket_connect_timeout=5,
                socket_timeout=10,
            )
            self._r.ping()
        except redis.RedisError as e:
            self._r = None
            host_port = f"{self._config.redis_host}:{self._config.redis_port}"
            raise RedisClientError(f"Cannot connect to Redis at {host_port}: {e}") from e

    def disconnect(self) -> None:
        if self._r:
            self._r.close()
            self._r = None

    def reconnect_on_failure(self) -> bool:
        """Attempt reconnect. Returns True if connected after the call."""
        try:
            if self._r:
                self._r.ping()
                return True
        except redis.RedisError:
            pass
        try:
            self.connect()
            return True
        except RedisClientError:
            return False

    @property
    def hostname(self) -> str:
        return self._hostname

    # ---- Health ----

    def write_health(self, uptime_s: float | None = None, os_name: str | None = None) -> None:
        payload: dict = {
            "hostname": self._hostname,
            "last_seen_ts": time.time(),
        }
        if uptime_s is not None:
            payload["uptime_seconds"] = uptime_s
        if os_name is not None:
            payload["os_name"] = os_name
        self._cmd(
            "set",
            f"kpidash:client:{self._hostname}:health",
            json.dumps(payload),
            ex=5,  # HEALTH_TTL_S
        )
        self._cmd("sadd", "kpidash:clients", self._hostname)

    # ---- Telemetry ----

    def write_telemetry(self, telemetry: dict) -> None:
        payload = dict(telemetry)
        payload["hostname"] = self._hostname
        payload["ts"] = time.time()
        self._cmd(
            "set",
            f"kpidash:client:{self._hostname}:telemetry",
            json.dumps(payload),
            ex=15,  # TELEMETRY_TTL_S
        )

    def write_dev_telemetry(self, telemetry: dict) -> None:
        """Write fast-poll GPU+CPU+RAM data to a dedicated dev key."""
        payload = dict(telemetry)
        payload["hostname"] = self._hostname
        payload["ts"] = time.time()
        self._cmd(
            "set",
            f"kpidash:client:{self._hostname}:dev_telemetry",
            json.dumps(payload),
            ex=5,  # short TTL — only useful while graph is active
        )

    # ---- Repos ----

    def write_repos(self, repo_statuses: list[dict]) -> None:
        """Write all non-clean repos as fields in the repos hash."""
        key = f"kpidash:repos:{self._hostname}"
        pipe = self._r.pipeline()
        # Build new field set
        new_paths = {r["path"] for r in repo_statuses}
        # Delete stale entries
        existing = self._r.hkeys(key)
        for p in existing:
            if p not in new_paths:
                pipe.hdel(key, p)
        # Write active entries
        for r in repo_statuses:
            pipe.hset(key, r["path"], json.dumps(r))
        if repo_statuses:
            pipe.expire(key, 30)  # REPOS_TTL_S
        pipe.execute()

    # ---- Activities ----

    def start_activity(self, name: str) -> str:
        activity_id = str(uuid.uuid4())
        now = time.time()
        pipe = self._r.pipeline()
        pipe.hset(
            f"kpidash:activity:{activity_id}",
            mapping={
                "activity_id": activity_id,
                "host": self._hostname,
                "name": name,
                "state": "active",
                "start_ts": str(now),
            },
        )
        pipe.zadd("kpidash:activities", {activity_id: now})
        pipe.zremrangebyrank("kpidash:activities", 0, -(ACTIVITY_ZSET_TRIM + 1))
        pipe.execute()
        return activity_id

    def end_activity(self, activity_id: str) -> None:
        self._cmd(
            "hset",
            f"kpidash:activity:{activity_id}",
            mapping={
                "state": "done",
                "end_ts": str(time.time()),
            },
        )

    # ---- Fortune ----

    def push_fortune(self, text: str, duration: int = 300) -> None:
        payload = json.dumps(
            {
                "text": text,
                "source": "pushed",
                "pushed_by": self._hostname,
            }
        )
        self._cmd("set", "kpidash:fortune:pushed", payload, ex=duration)

    # ---- Status / Log path ----

    def get_status_current(self) -> dict | None:
        raw = self._cmd("get", "kpidash:status:current")
        if raw:
            try:
                return json.loads(raw)
            except json.JSONDecodeError:
                return None
        return None

    def ack_status(self, message_id: str) -> None:
        self._cmd("set", f"kpidash:status:ack:{message_id}", "1", ex=60)  # STATUS_ACK_TTL_S

    def get_logpath(self) -> str | None:
        return self._cmd("get", "kpidash:system:logpath")

    # ---- Internal ----

    def _cmd(self, method: str, *args, **kwargs):
        if not self._r:
            raise RedisClientError("Not connected")
        try:
            return getattr(self._r, method)(*args, **kwargs)
        except redis.RedisError as e:
            raise RedisClientError(f"Redis command failed: {e}") from e
