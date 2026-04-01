"""tests/test_tools.py — T046

Unit tests for the MCP tool implementations.

Redis is fully mocked so these tests run without a live server.
"""

from __future__ import annotations

import time
from unittest.mock import MagicMock, patch

import pytest

# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------


def _make_mock_client(
    *, start_result: dict | None = None, end_result: dict | None = None, raise_on: str | None = None
):
    """
    Return a mock MCPRedisClient factory that the tests can inject via
    `patch("kpidash_mcp.tools.MCPRedisClient", ...)`.
    """
    from kpidash_mcp.redis_client import MCPRedisError

    instance = MagicMock()
    if raise_on == "start":
        instance.start_activity.side_effect = MCPRedisError("Redis down")
    elif start_result is not None:
        instance.start_activity.return_value = start_result

    if raise_on == "end":
        instance.end_activity.side_effect = MCPRedisError("not found")
    elif end_result is not None:
        instance.end_activity.return_value = end_result

    return MagicMock(return_value=instance)


# ---------------------------------------------------------------------------
# start_activity
# ---------------------------------------------------------------------------


def test_start_activity_returns_dict():
    now = time.time()
    expected = {
        "activity_id": "uuid-abc",
        "host": "testhost",
        "name": "Build feature",
        "state": "active",
        "start_ts": now,
    }
    factory = _make_mock_client(start_result=expected)

    with patch("kpidash_mcp.tools.MCPRedisClient", factory):
        from kpidash_mcp.tools import start_activity

        result = start_activity(name="Build feature", host="testhost")

    assert result["activity_id"] == "uuid-abc"
    assert result["state"] == "active"


def test_start_activity_default_host_omitted():
    now = time.time()
    expected = {
        "activity_id": "uuid-xyz",
        "host": "mybox",
        "name": "Task",
        "state": "active",
        "start_ts": now,
    }
    factory = _make_mock_client(start_result=expected)

    with patch("kpidash_mcp.tools.MCPRedisClient", factory):
        from kpidash_mcp.tools import start_activity

        start_activity(name="Task")

    # host not passed — MCPRedisClient.start_activity was called with host=None
    instance = factory.return_value
    _, kwargs = instance.start_activity.call_args
    assert kwargs.get("host") is None or kwargs["host"] is None


def test_start_activity_raises_valueerror_on_redis_error():
    factory = _make_mock_client(raise_on="start")

    with patch("kpidash_mcp.tools.MCPRedisClient", factory):
        from kpidash_mcp.tools import start_activity

        with pytest.raises(ValueError, match="Redis down"):
            start_activity(name="Fail task", host="h")


# ---------------------------------------------------------------------------
# end_activity
# ---------------------------------------------------------------------------


def test_end_activity_returns_done_dict():
    now = time.time()
    expected = {
        "activity_id": "uuid-done",
        "state": "done",
        "end_ts": now,
        "duration_seconds": 120.0,
    }
    factory = _make_mock_client(end_result=expected)

    with patch("kpidash_mcp.tools.MCPRedisClient", factory):
        from kpidash_mcp.tools import end_activity

        result = end_activity(activity_id="uuid-done")

    assert result["state"] == "done"
    assert result["duration_seconds"] == 120.0


def test_end_activity_raises_valueerror_when_not_found():
    factory = _make_mock_client(raise_on="end")

    with patch("kpidash_mcp.tools.MCPRedisClient", factory):
        from kpidash_mcp.tools import end_activity

        with pytest.raises(ValueError, match="not found"):
            end_activity(activity_id="missing-uuid")


# ---------------------------------------------------------------------------
# Redis write verification via MCPRedisClient unit tests
# ---------------------------------------------------------------------------


def test_mcp_redis_client_start_writes_hset_and_zadd():
    mock_redis = MagicMock()
    pipe = MagicMock()
    mock_redis.pipeline.return_value = pipe

    with patch("redis.Redis", return_value=mock_redis):
        from kpidash_mcp.redis_client import MCPRedisClient

        client = MCPRedisClient()
        client.start_activity(name="Test", host="box")

    pipe.hset.assert_called_once()
    pipe.zadd.assert_called_once()
    pipe.zremrangebyrank.assert_called_once()

    # Verify key format
    args, kwargs = pipe.hset.call_args
    assert args[0].startswith("kpidash:activity:")
    mapping = kwargs.get("mapping", {})
    assert mapping["name"] == "Test"
    assert mapping["host"] == "box"
    assert mapping["state"] == "active"


def test_mcp_redis_client_end_writes_hset():
    mock_redis = MagicMock()
    mock_redis.hgetall.return_value = {
        "activity_id": "abc",
        "host": "box",
        "name": "Work",
        "state": "active",
        "start_ts": str(time.time() - 60),
    }

    with patch("redis.Redis", return_value=mock_redis):
        from kpidash_mcp.redis_client import MCPRedisClient

        client = MCPRedisClient()
        result = client.end_activity("abc")

    mock_redis.hset.assert_called_once()
    args, kwargs = mock_redis.hset.call_args
    assert args[0] == "kpidash:activity:abc"
    mapping = kwargs.get("mapping", {})
    assert mapping["state"] == "done"
    assert float(mapping["end_ts"]) == pytest.approx(time.time(), abs=2.0)
    assert result["duration_seconds"] == pytest.approx(60.0, abs=2.0)


def test_mcp_redis_client_end_raises_when_not_found():
    mock_redis = MagicMock()
    mock_redis.hgetall.return_value = {}  # empty dict = not found

    with patch("redis.Redis", return_value=mock_redis):
        from kpidash_mcp.redis_client import MCPRedisClient, MCPRedisError

        client = MCPRedisClient()
        with pytest.raises(MCPRedisError, match="Activity not found"):
            client.end_activity("ghost-id")
