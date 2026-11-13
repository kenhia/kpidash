"""tests/test_cli.py — the CLI surface 2.0.0 removed stays removed (sprint 036)."""

from __future__ import annotations

import pytest
from click.testing import CliRunner

from kpidash_client.cli import main


@pytest.mark.parametrize("command", ["activity", "fortune"])
def test_retired_command_groups_are_gone(command):
    result = CliRunner().invoke(main, [command, "--help"])

    assert result.exit_code == 2
    assert "No such command" in result.output


def test_the_remaining_commands_are_registered():
    assert {"daemon", "status", "log-path", "net", "service-status"} <= set(main.commands)
