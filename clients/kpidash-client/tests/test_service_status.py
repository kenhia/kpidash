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
    assert key == "kpidash:services:api:_"
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
    key, raw = mock_client._r.set.call_args[0]
    assert key == "kpidash:services:db:kai"
    payload = json.loads(raw)
    assert payload["host"] == "kai"
    assert payload["icon"] == 7


def test_set_service_status_writes_layout(mock_client):
    """WI #4847: title, lines and span ride the same key, as given."""
    lines = [{"t": "blip", "size": "S", "color": "#1e66f5"}, "", "OK backups"]
    mock_client.set_service_status(
        name="argus", state="ok", text="2 ok", host="kai", title="argus", lines=lines, span=2
    )
    _key, raw = mock_client._r.set.call_args[0]
    payload = json.loads(raw)
    assert payload["title"] == "argus"
    assert payload["lines"] == lines
    assert payload["span"] == 2
    assert payload["text"] == "2 ok"  # still required: the fallback


def test_set_service_status_omits_absent_layout(mock_client):
    mock_client.set_service_status(name="x", state="ok", text="t", lines=[])
    _key, raw = mock_client._r.set.call_args[0]
    payload = json.loads(raw)
    assert not {"title", "lines", "span"} & set(payload)


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
        name="api", state="ok", text="hi", host="_", icon=None,
        title=None, lines=None, span=None,
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
        name="db", state="maintenance", text="draining", host="kai", icon=3,
        title=None, lines=None, span=None,
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


# ---------------------------------------------------------------------------
# WI #4847: --title, --line, --span
# ---------------------------------------------------------------------------


def _invoke(args):
    from kpidash_client.cli import main

    runner = CliRunner()
    p, fake = _patch_make_client()
    with p, patch("kpidash_client.cli._load_config"):
        r = runner.invoke(
            main, ["service-status", "--name", "argus", "--state", "ok", "--text", "t", *args]
        )
    return r, fake


def test_cli_service_status_lines():
    r, fake = _invoke(
        [
            "--title", "argus", "--span", "2",
            "--line", "[size=s color=#1E66F5] blip 12m ago",
            "--line", "",
            "--line", "[align=L] OK   backups",
            "--line", "plain words [not a style]",
        ]
    )
    assert r.exit_code == 0, r.output
    kw = fake.set_service_status.call_args.kwargs
    assert kw["title"] == "argus"
    assert kw["span"] == 2
    assert kw["lines"] == [
        {"t": "blip 12m ago", "size": "S", "color": "#1e66f5"},
        "",
        {"t": "OK   backups", "align": "L"},
        "plain words [not a style]",
    ]


def test_cli_service_status_empty_style_group_is_an_object():
    r, fake = _invoke(["--line", "[] centred"])
    assert r.exit_code == 0, r.output
    assert fake.set_service_status.call_args.kwargs["lines"] == [{"t": "centred"}]


@pytest.mark.parametrize(
    "bad",
    [
        ["--line", "[size=XL] x"],
        ["--line", "[align=middle] x"],
        ["--line", "[color=red] x"],
        ["--line", "[weight=bold] x"],
        ["--line", "[size] x"],
        ["--line", "two\nlines"],
        ["--title", "two\nlines"],
        ["--span", "3"],
        [arg for i in range(9) for arg in ("--line", str(i))],
    ],
)
def test_cli_service_status_rejects_bad_layout(bad):
    r, fake = _invoke(bad)
    assert r.exit_code != 0, r.output
    fake.set_service_status.assert_not_called()
