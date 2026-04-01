"""
redis_client.py — Minimal Redis helper for kpidash-mcp (T043)

Opens a Redis connection per client instance (stateless between MCP tool calls).
Connection settings come from environment variables only.
"""

from __future__ import annotations

import os
import socket
import time
import uuid

import redis


class MCPRedisError(Exception):
    """Raised when a Redis operation fails inside the MCP server."""


class MCPRedisClient:
    """Short-lived Redis client used by MCP tool implementations."""

    ACTIVITY_ZSET_TRIM = 20  # keep 2× display max in the sorted set

    def __init__(self) -> None:
        host = os.environ.get("KPIDASH_REDIS_HOST", "localhost")
        port = int(os.environ.get("KPIDASH_REDIS_PORT", "6379"))
        auth = os.environ.get("REDISCLI_AUTH")
        try:
            self._r = redis.Redis(
                host=host,
                port=port,
                password=auth,
                decode_responses=True,
                socket_connect_timeout=5,
                socket_timeout=10,
            )
            self._r.ping()
        except redis.RedisError as e:
            raise MCPRedisError(f"Cannot connect to Redis at {host}:{port}: {e}") from e

    # ------------------------------------------------------------------
    # start_activity
    # ------------------------------------------------------------------

    def start_activity(self, name: str, host: str | None = None) -> dict:
        """
        Create a new activity entry in Redis and return its metadata.

        Raises MCPRedisError on validation failure or Redis error.
        """
        if len(name) > 127:
            raise MCPRedisError(f"Activity name too long: {len(name)} chars (max 127)")

        if host is None:
            host = socket.gethostname().split(".")[0].lower()
        if len(host) > 63:
            raise MCPRedisError(f"Host name too long: {len(host)} chars (max 63)")

        activity_id = str(uuid.uuid4())
        now = time.time()

        try:
            pipe = self._r.pipeline()
            pipe.hset(
                f"kpidash:activity:{activity_id}",
                mapping={
                    "activity_id": activity_id,
                    "host": host,
                    "name": name,
                    "state": "active",
                    "start_ts": str(now),
                },
            )
            pipe.zadd("kpidash:activities", {activity_id: now})
            pipe.zremrangebyrank("kpidash:activities", 0, -(self.ACTIVITY_ZSET_TRIM + 1))
            pipe.execute()
        except redis.RedisError as e:
            raise MCPRedisError(f"Redis write failed: {e}") from e

        return {
            "activity_id": activity_id,
            "host": host,
            "name": name,
            "state": "active",
            "start_ts": now,
        }

    # ------------------------------------------------------------------
    # end_activity
    # ------------------------------------------------------------------

    def end_activity(self, activity_id: str) -> dict:
        """
        Mark an activity as done and return its completion metadata.

        Raises MCPRedisError if the activity is not found or Redis fails.
        """
        key = f"kpidash:activity:{activity_id}"
        try:
            raw = self._r.hgetall(key)
        except redis.RedisError as e:
            raise MCPRedisError(f"Redis read failed: {e}") from e

        if not raw:
            raise MCPRedisError(f"Activity not found: {activity_id!r}")

        now = time.time()
        start_ts = float(raw.get("start_ts", now))
        duration = now - start_ts

        try:
            self._r.hset(key, mapping={"state": "done", "end_ts": str(now)})
        except redis.RedisError as e:
            raise MCPRedisError(f"Redis write failed: {e}") from e

        return {
            "activity_id": activity_id,
            "state": "done",
            "end_ts": now,
            "duration_seconds": duration,
        }
