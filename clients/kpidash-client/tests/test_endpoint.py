"""tests/test_endpoint.py -- resolving the Redis endpoint through khlenv."""

from __future__ import annotations

from unittest.mock import MagicMock, patch

import pytest
from khlenv import KhlenvUnavailable

from kpidash_client.config import ClientConfig
from kpidash_client.endpoint import (
    DEFAULT_ENDPOINT,
    KHLENV_APP,
    KHLENV_KEY,
    EndpointError,
    parse_endpoint,
    resolve_redis_endpoint,
)

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
