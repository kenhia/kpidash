"""tests/test_service_status.py — T027

Tests the service-status CLI command and RedisClient.set_service_status.
"""

from __future__ import annotations

import json
from unittest.mock import MagicMock, patch

import pytest
from click.testing import CliRunner


@pytest.fixture
def mock_client():
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
# RedisClient.set_service_status
# ---------------------------------------------------------------------------


def test_set_service_status_writes_required_fields(mock_client):
    mock_client.set_service_status(name="api", state="ok", text="all good")
    mock_client._r.set.assert_called_once()
    key, raw = mock_client._r.set.call_args[0]
    assert key == "kpidash:services:api"
    payload = json.loads(raw)
    assert payload["state"] == "ok"
    assert payload["text"] == "all good"
    assert isinstance(payload["ts"], (int, float))
    assert "host" not in payload
    assert "icon" not in payload


def test_set_service_status_includes_optional_fields(mock_client):
    mock_client.set_service_status(
        name="db", state="unhealthy", text="lag 5s", host="kai", icon=7
    )
    raw = mock_client._r.set.call_args[0][1]
    payload = json.loads(raw)
    assert payload["host"] == "kai"
    assert payload["icon"] == 7


def test_set_service_status_no_ttl(mock_client):
    mock_client.set_service_status(name="x", state="ok", text="t")
    # `ex` keyword must NOT be present (set without TTL per data-model).
    assert "ex" not in mock_client._r.set.call_args.kwargs


# ---------------------------------------------------------------------------
# CLI: kpidash-client service-status
# ---------------------------------------------------------------------------


def _patch_make_client():
    """Patch _make_client to return a mock with .set_service_status."""
    fake = MagicMock()
    return patch("kpidash_client.cli._make_client", return_value=fake), fake


def test_cli_service_status_minimal():
    from kpidash_client.cli import main

    runner = CliRunner()
    p, fake = _patch_make_client()
    with p, patch("kpidash_client.cli._load_config"):
        r = runner.invoke(
            main,
            ["service-status", "--name", "api", "--state", "ok", "--text", "hi"],
        )
    assert r.exit_code == 0, r.output
    fake.set_service_status.assert_called_once_with(
        name="api", state="ok", text="hi", host=None, icon=None
    )


def test_cli_service_status_full():
    from kpidash_client.cli import main

    runner = CliRunner()
    p, fake = _patch_make_client()
    with p, patch("kpidash_client.cli._load_config"):
        r = runner.invoke(
            main,
            [
                "service-status",
                "--name", "db", "--state", "MAINTENANCE",
                "--text", "draining", "--host", "kai", "--icon", "3",
            ],
        )
    assert r.exit_code == 0, r.output
    fake.set_service_status.assert_called_once_with(
        name="db", state="maintenance", text="draining", host="kai", icon=3
    )


def test_cli_service_status_rejects_bad_state():
    from kpidash_client.cli import main

    runner = CliRunner()
    p, _fake = _patch_make_client()
    with p, patch("kpidash_client.cli._load_config"):
        r = runner.invoke(
            main,
            ["service-status", "--name", "x", "--state", "frobnicated", "--text", "t"],
        )
    assert r.exit_code != 0
    assert "Invalid value for '--state'" in r.output or "invalid choice" in r.output.lower()


def test_cli_service_status_requires_name():
    from kpidash_client.cli import main

    runner = CliRunner()
    r = runner.invoke(main, ["service-status", "--state", "ok", "--text", "x"])
    assert r.exit_code != 0


def test_cli_service_status_requires_text():
    from kpidash_client.cli import main

    runner = CliRunner()
    r = runner.invoke(main, ["service-status", "--name", "x", "--state", "ok"])
    assert r.exit_code != 0
