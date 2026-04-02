"""tests/test_activity.py — T029

Tests for activity start/done Redis operations and state.json persistence.
"""

from __future__ import annotations

import json
import time
from unittest.mock import MagicMock

import pytest


@pytest.fixture
def mock_client():
    """Return a RedisClient with a mocked redis.Redis backend."""
    from kpidash_client.config import ClientConfig
    from kpidash_client.redis_client import RedisClient

    cfg = MagicMock(spec=ClientConfig)
    cfg.redis_host = "localhost"
    cfg.redis_port = 6379
    cfg.hostname = None

    client = RedisClient(cfg)
    client._r = MagicMock()
    client._hostname = "testhost"
    return client


# ---------------------------------------------------------------------------
# start_activity
# ---------------------------------------------------------------------------


def test_start_activity_returns_uuid(mock_client):
    pipe = MagicMock()
    mock_client._r.pipeline.return_value = pipe

    activity_id = mock_client.start_activity("Build feature")

    # Must look like a UUID
    assert len(activity_id) == 36
    assert activity_id.count("-") == 4


def test_start_activity_writes_hset(mock_client):
    pipe = MagicMock()
    mock_client._r.pipeline.return_value = pipe

    activity_id = mock_client.start_activity("My task")

    pipe.hset.assert_called_once()
    args, kwargs = pipe.hset.call_args
    assert args[0] == f"kpidash:activity:{activity_id}"
    mapping = kwargs.get("mapping", {})
    assert mapping["host"] == "testhost"
    assert mapping["name"] == "My task"
    assert mapping["state"] == "active"
    assert "start_ts" in mapping


def test_start_activity_calls_zadd(mock_client):
    pipe = MagicMock()
    mock_client._r.pipeline.return_value = pipe

    activity_id = mock_client.start_activity("Something")

    pipe.zadd.assert_called_once()
    args, _ = pipe.zadd.call_args
    assert args[0] == "kpidash:activities"
    score = args[1][activity_id]
    assert score == pytest.approx(time.time(), abs=2.0)


def test_start_activity_trims_sorted_set(mock_client):
    pipe = MagicMock()
    mock_client._r.pipeline.return_value = pipe

    from kpidash_client.redis_client import ACTIVITY_ZSET_TRIM

    mock_client.start_activity("Trim test")

    pipe.zremrangebyrank.assert_called_once()
    _, kwargs = pipe.zremrangebyrank.call_args
    # Trim keeps ACTIVITY_ZSET_TRIM entries (rank -(TRIM+1) removes oldest)
    args, _ = pipe.zremrangebyrank.call_args
    assert args[0] == "kpidash:activities"
    assert args[1] == 0
    assert args[2] == -(ACTIVITY_ZSET_TRIM + 1)


# ---------------------------------------------------------------------------
# end_activity
# ---------------------------------------------------------------------------


def test_end_activity_sets_state_done(mock_client):
    mock_client._r.hset.return_value = 0
    mock_client.end_activity("abc-uuid-1234")

    mock_client._r.hset.assert_called_once()
    args, kwargs = mock_client._r.hset.call_args
    assert "kpidash:activity:abc-uuid-1234" in args
    mapping = kwargs.get("mapping", {})
    assert mapping.get("state") == "done"


def test_end_activity_records_end_ts(mock_client):
    mock_client._r.hset.return_value = 0
    before = time.time()
    mock_client.end_activity("my-activity")
    after = time.time()

    _, kwargs = mock_client._r.hset.call_args
    mapping = kwargs.get("mapping", {})
    end_ts = float(mapping.get("end_ts", 0))
    assert before <= end_ts <= after


# ---------------------------------------------------------------------------
# state.json persistence
# ---------------------------------------------------------------------------


def test_state_file_persists_activity_id(tmp_path):
    """state.json must roundtrip activity_id successfully."""
    state_file = tmp_path / "state.json"
    state_file.write_text(json.dumps({"activity_id": "stored-uuid-9999"}))

    data = json.loads(state_file.read_text())
    assert data["activity_id"] == "stored-uuid-9999"


def test_state_file_cleared_after_done(tmp_path):
    """After ending an activity, activity_id should be removed from state."""
    state_file = tmp_path / "state.json"
    state = {"activity_id": "to-be-cleared"}
    state.pop("activity_id", None)
    state_file.write_text(json.dumps(state))

    data = json.loads(state_file.read_text())
    assert "activity_id" not in data
