"""tests/test_endpoint.py -- resolving the Redis endpoint through khlenv."""

from __future__ import annotations

import json
from unittest.mock import MagicMock, patch

import pytest
from khlenv import KhlenvUnavailable

from kpidash_client.config import ClientConfig
from kpidash_client.endpoint import (
    DEFAULT_ENDPOINT,
    KHLENV_APP,
    KHLENV_KEY,
    EndpointError,
    khlenv_on_fallback,
    parse_endpoint,
    resolve_redis_endpoint,
)
from kpidash_client.redis_client import RedisClient

# --- parse_endpoint --------------------------------------------------------


@pytest.mark.parametrize(
    ("value", "expected"),
    [
        ("rpi53:6379", ("rpi53", 6379)),
        ("rpi53", ("rpi53", 6379)),
        ("  rpi53:6380  ", ("rpi53", 6380)),
        ("10.0.0.5:6379", ("10.0.0.5", 6379)),
        ("rpi53.encke-wahoo.ts.net:6379", ("rpi53.encke-wahoo.ts.net", 6379)),
    ],
)
def test_parse_endpoint_accepts(value, expected):
    assert parse_endpoint(value) == expected


@pytest.mark.parametrize("value", ["", "   ", ":6379", "rpi53:nope", "rpi53:0", "rpi53:70000"])
def test_parse_endpoint_rejects(value):
    with pytest.raises(EndpointError):
        parse_endpoint(value)


# --- resolve_redis_endpoint ------------------------------------------------


def test_the_config_override_wins_and_khlenv_is_never_asked():
    """Every existing install keeps its `[redis] host` and its behaviour."""
    config = ClientConfig(redis_host="192.168.1.100", redis_port=6380)
    with patch("kpidash_client.endpoint.Khlenv") as khlenv:
        assert resolve_redis_endpoint(config) == ("192.168.1.100", 6380)
    khlenv.assert_not_called()


def test_the_config_override_defaults_its_port():
    config = ClientConfig(redis_host="192.168.1.100")
    assert resolve_redis_endpoint(config) == ("192.168.1.100", 6379)


def test_without_an_override_the_endpoint_comes_from_khlenv():
    config = ClientConfig()
    client = MagicMock()
    client.get.return_value = "rpi53:6379"
    with patch("kpidash_client.endpoint.Khlenv", return_value=client) as khlenv:
        assert resolve_redis_endpoint(config) == ("rpi53", 6379)

    khlenv.assert_called_once_with(KHLENV_APP)
    client.get.assert_called_once_with(KHLENV_KEY, default=DEFAULT_ENDPOINT)


def test_a_moved_redis_is_picked_up_from_khlenv():
    config = ClientConfig()
    client = MagicMock()
    client.get.return_value = "kubsdb:6390"
    with patch("kpidash_client.endpoint.Khlenv", return_value=client):
        assert resolve_redis_endpoint(config) == ("kubsdb", 6390)


def test_an_explicit_null_from_khlenv_is_an_error_not_a_default():
    """A null means 'intentionally no endpoint' -- connecting anyway would
    override a deliberate decision recorded in the store."""
    config = ClientConfig()
    client = MagicMock()
    client.get.return_value = None
    with patch("kpidash_client.endpoint.Khlenv", return_value=client):
        with pytest.raises(EndpointError, match="explicit null"):
            resolve_redis_endpoint(config)


def test_a_khlenv_failure_surfaces_as_an_endpoint_error():
    config = ClientConfig()
    client = MagicMock()
    client.get.side_effect = KhlenvUnavailable("khlenv: could not reach http://kubs0:7770")
    with patch("kpidash_client.endpoint.Khlenv", return_value=client):
        with pytest.raises(EndpointError, match=KHLENV_KEY):
            resolve_redis_endpoint(config)


def test_a_malformed_stored_value_surfaces_as_an_endpoint_error():
    config = ClientConfig()
    client = MagicMock()
    client.get.return_value = "rpi53:not-a-port"
    with patch("kpidash_client.endpoint.Khlenv", return_value=client):
        with pytest.raises(EndpointError, match="not a number"):
            resolve_redis_endpoint(config)


# --- khlenv_on_fallback / the retry (WI 4353) --------------------------------


def _cache(tmp_path, monkeypatch, payload):
    monkeypatch.setenv("KHLENV_CACHE_DIR", str(tmp_path))
    monkeypatch.setenv("KHLENV_ENDPOINT", "http://khlenv.invalid:7770")
    if payload is not None:
        (tmp_path / f"{KHLENV_APP}.json").write_text(json.dumps(payload))


GRATCH_2026_11_05 = {
    "endpoint": "http://kubs0.encke-wahoo.ts.net:7770",
    "fetched_at": "2026-11-04T01:12:53Z",
    "last_failover": {
        "at": "2026-11-05T23:24:18Z",
        "cache_age_seconds": 166285,
        "key": "KPIDASH_REDIS",
        "source": "cache",
    },
    "values": {"KPIDASH_REDIS": "rpi53:6379"},
}


def test_the_gratch_cache_reads_as_a_fallback(tmp_path, monkeypatch):
    """The exact file gratch held while kmon reported it (WI 4353)."""
    _cache(tmp_path, monkeypatch, GRATCH_2026_11_05)
    assert khlenv_on_fallback()


def test_a_live_fetch_after_the_failover_is_recovered(tmp_path, monkeypatch):
    _cache(tmp_path, monkeypatch, {**GRATCH_2026_11_05, "fetched_at": "2026-11-10T02:00:00Z"})
    assert not khlenv_on_fallback()


def test_a_same_second_tie_still_counts_as_a_fallback(tmp_path, monkeypatch):
    payload = {**GRATCH_2026_11_05, "fetched_at": GRATCH_2026_11_05["last_failover"]["at"]}
    _cache(tmp_path, monkeypatch, payload)
    assert khlenv_on_fallback()


@pytest.mark.parametrize(
    "payload",
    [None, {"fetched_at": "2026-11-04T01:12:53Z", "values": {}}, {"last_failover": None}],
)
def test_no_failover_record_is_not_a_fallback(tmp_path, monkeypatch, payload):
    _cache(tmp_path, monkeypatch, payload)
    assert not khlenv_on_fallback()


def test_a_fallback_with_no_live_fetch_ever_is_a_fallback(tmp_path, monkeypatch):
    payload = {"last_failover": {"at": "2026-11-05T23:24:18Z", "source": "default"}}
    _cache(tmp_path, monkeypatch, payload)
    assert khlenv_on_fallback()


def test_the_retry_re_resolves_only_while_on_a_fallback():
    rc = RedisClient(ClientConfig())
    rc._endpoint = ("rpi53", 6379)
    with (
        patch("kpidash_client.redis_client.khlenv_on_fallback", return_value=False),
        patch("kpidash_client.redis_client.resolve_redis_endpoint") as resolve,
    ):
        assert rc.refresh_endpoint_if_fallback() is False
    resolve.assert_not_called()


def test_the_retry_keeps_the_connection_when_the_endpoint_is_unchanged():
    rc = RedisClient(ClientConfig())
    rc._endpoint = ("rpi53", 6379)
    with (
        patch("kpidash_client.redis_client.khlenv_on_fallback", return_value=True),
        patch("kpidash_client.redis_client.resolve_redis_endpoint", return_value=("rpi53", 6379)),
        patch.object(rc, "connect") as connect,
    ):
        assert rc.refresh_endpoint_if_fallback() is True
    connect.assert_not_called()


def test_the_retry_reconnects_when_khlenv_names_a_new_endpoint():
    rc = RedisClient(ClientConfig())
    rc._endpoint = ("rpi53", 6379)
    with (
        patch("kpidash_client.redis_client.khlenv_on_fallback", return_value=True),
        patch("kpidash_client.redis_client.resolve_redis_endpoint", return_value=("kubsdb", 6390)),
        patch.object(rc, "connect") as connect,
    ):
        assert rc.refresh_endpoint_if_fallback() is True
    connect.assert_called_once()


def test_the_retry_never_runs_under_a_config_override():
    rc = RedisClient(ClientConfig(redis_host="192.168.1.100"))
    with patch("kpidash_client.redis_client.khlenv_on_fallback") as on_fallback:
        assert rc.refresh_endpoint_if_fallback() is False
    on_fallback.assert_not_called()


def test_a_real_khlenv_round_trip_clears_the_fallback(tmp_path, monkeypatch):
    """End to end through the real khlenv client: the cache gratch held, then
    a live answer from a stub service, then the on-fallback test again."""
    import threading
    from http.server import BaseHTTPRequestHandler, HTTPServer

    class Handler(BaseHTTPRequestHandler):
        def do_GET(self):  # noqa: N802 - http.server's name
            body = b"rpi53:6379"
            self.send_response(200)
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)

        def log_message(self, *args):
            pass

    server = HTTPServer(("127.0.0.1", 0), Handler)
    thread = threading.Thread(target=server.serve_forever, daemon=True)
    thread.start()
    try:
        _cache(tmp_path, monkeypatch, GRATCH_2026_11_05)
        monkeypatch.setenv("KHLENV_ENDPOINT", f"http://127.0.0.1:{server.server_port}")
        rc = RedisClient(ClientConfig())
        rc._endpoint = ("rpi53", 6379)
        assert khlenv_on_fallback()
        assert rc.refresh_endpoint_if_fallback() is True
        assert not khlenv_on_fallback()
    finally:
        server.shutdown()
