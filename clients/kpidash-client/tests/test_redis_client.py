"""tests/test_redis_client.py — Unit tests for RedisClient (T014)"""

from __future__ import annotations

import json
from unittest.mock import MagicMock, patch

from kpidash_client.config import ClientConfig
from kpidash_client.redis_client import RedisClient

MINIMAL_TOML = "[redis]\nhost = '127.0.0.1'\n"


def make_client(tmp_path, env_auth=None) -> tuple[RedisClient, MagicMock]:
    cfg_file = tmp_path / "config.toml"
    cfg_file.write_text(MINIMAL_TOML)
    cfg = ClientConfig.load(cfg_file)

    client = RedisClient(cfg)

    mock_redis = MagicMock()
    mock_redis.ping.return_value = True
    client._r = mock_redis
    client._hostname = "testhost"
    return client, mock_redis


def test_write_health_sets_key(tmp_path, monkeypatch):
    monkeypatch.delenv("REDISCLI_AUTH", raising=False)
    client, mock_r = make_client(tmp_path)
    client.write_health(uptime_s=3600.0)

    # Should call SET with EX=5 and SADD
    set_calls = [c for c in mock_r.set.call_args_list]
    assert len(set_calls) == 1
    key, value = set_calls[0].args[:2]
    assert key == "kpidash:client:testhost:health"
    payload = json.loads(value)
    assert payload["hostname"] == "testhost"
    assert payload["uptime_seconds"] == 3600.0
    assert set_calls[0].kwargs["ex"] == 5

    mock_r.sadd.assert_called_once_with("kpidash:clients", "testhost")


def test_write_health_includes_os_name(tmp_path, monkeypatch):
    monkeypatch.delenv("REDISCLI_AUTH", raising=False)
    client, mock_r = make_client(tmp_path)
    client.write_health(uptime_s=100.0, os_name="Linux 6.1.0")

    value = mock_r.set.call_args.args[1]
    payload = json.loads(value)
    assert payload["os_name"] == "Linux 6.1.0"


def test_write_health_omits_os_name_when_none(tmp_path, monkeypatch):
    monkeypatch.delenv("REDISCLI_AUTH", raising=False)
    client, mock_r = make_client(tmp_path)
    client.write_health(uptime_s=100.0)

    value = mock_r.set.call_args.args[1]
    payload = json.loads(value)
    assert "os_name" not in payload


def test_write_telemetry_sets_key(tmp_path):
    client, mock_r = make_client(tmp_path)
    client.write_telemetry({"cpu_pct": 50.0, "ram_used_mb": 1024, "ram_total_mb": 4096})

    assert mock_r.set.called
    key = mock_r.set.call_args.args[0]
    assert key == "kpidash:client:testhost:telemetry"
    payload = json.loads(mock_r.set.call_args.args[1])
    assert payload["cpu_pct"] == 50.0
    assert payload["hostname"] == "testhost"
    assert mock_r.set.call_args.kwargs["ex"] == 15


def test_start_activity(tmp_path):
    client, mock_r = make_client(tmp_path)
    mock_pipe = MagicMock()
    mock_r.pipeline.return_value = mock_pipe

    activity_id = client.start_activity("Test task")

    assert len(activity_id) == 36  # UUID4 format
    mock_pipe.hset.assert_called_once()
    hset_args = mock_pipe.hset.call_args
    assert hset_args.args[0] == f"kpidash:activity:{activity_id}"
    mapping = hset_args.kwargs["mapping"]
    assert mapping["host"] == "testhost"
    assert mapping["name"] == "Test task"
    assert mapping["state"] == "active"
    mock_pipe.zadd.assert_called_once()
    mock_pipe.zremrangebyrank.assert_called_once()
    mock_pipe.execute.assert_called_once()


def test_end_activity(tmp_path):
    client, mock_r = make_client(tmp_path)
    client.end_activity("some-uuid-1234")

    mock_r.hset.assert_called_once()
    mapping = mock_r.hset.call_args.kwargs["mapping"]
    assert mapping["state"] == "done"
    assert "end_ts" in mapping


def test_push_fortune_uses_duration(tmp_path):
    client, mock_r = make_client(tmp_path)
    client.push_fortune("Hello world", duration=600)

    mock_r.set.assert_called_once()
    key = mock_r.set.call_args.args[0]
    assert key == "kpidash:fortune:pushed"
    payload = json.loads(mock_r.set.call_args.args[1])
    assert payload["text"] == "Hello world"
    assert payload["source"] == "pushed"
    assert mock_r.set.call_args.kwargs["ex"] == 600


def test_push_fortune_default_duration(tmp_path):
    client, mock_r = make_client(tmp_path)
    client.push_fortune("Hello")
    assert mock_r.set.call_args.kwargs["ex"] == 300


def test_ack_status(tmp_path):
    client, mock_r = make_client(tmp_path)
    client.ack_status("msg-id-abc")
    mock_r.set.assert_called_once_with("kpidash:status:ack:msg-id-abc", "1", ex=60)


def test_get_logpath(tmp_path):
    client, mock_r = make_client(tmp_path)
    mock_r.get.return_value = "/var/log/kpidash/kpidash.log"
    result = client.get_logpath()
    assert result == "/var/log/kpidash/kpidash.log"
    mock_r.get.assert_called_once_with("kpidash:system:logpath")


def test_reconnect_on_failure(tmp_path, monkeypatch):
    monkeypatch.delenv("REDISCLI_AUTH", raising=False)
    cfg_file = tmp_path / "config.toml"
    cfg_file.write_text(MINIMAL_TOML)
    cfg = ClientConfig.load(cfg_file)
    client = RedisClient(cfg)

    # Simulate: first ping fails, then connect succeeds
    import redis as redis_lib  # noqa: PLC0415

    mock_r = MagicMock()
    mock_r.ping.side_effect = redis_lib.ConnectionError("refused")

    with patch("kpidash_client.redis_client.redis.Redis", return_value=mock_r):
        client._r = mock_r
        # reconnect_on_failure should call connect() which patches Redis
        # For this test, verify it handles the exception path
        result = client.reconnect_on_failure()
        # It will try to reconnect and also fail (mock_r.ping still raises)
        # but should not throw — returns bool
        assert isinstance(result, bool)
