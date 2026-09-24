"""tests/test_launchd.py — the macOS LaunchAgent (WI #3132)

These run on Linux, and that is the point: they exercise the template and the
launcher script as bytes, through `launchd/install.sh render`, without anything
reaching launchctl. `install.sh` refuses every mode but `render` off macOS, which
one test below asserts, so a stray call here cannot load an agent anywhere.

The launcher is tested by running the SCRIPT STRING TAKEN FROM THE RENDERED
PLIST — not a copy of it — so the thing tested is the thing launchd runs.
"""

from __future__ import annotations

import os
import plistlib
import subprocess
from pathlib import Path

import pytest

CLIENT_DIR = Path(__file__).resolve().parent.parent
INSTALLER = CLIENT_DIR / "launchd" / "install.sh"
LABEL = "net.kenhia.kpidash.client"
SECRETS = "/etc/khomelab/secrets.env"


def render(home: str = "/Users/tester", exec_path: str | None = None) -> dict:
    args = [str(INSTALLER), "render", "--home", home]
    if exec_path:
        args += ["--exec", exec_path]
    out = subprocess.run(args, check=True, capture_output=True)
    return plistlib.loads(out.stdout)


def launcher_argv(plist: dict, secrets_file: str, program: list[str]) -> list[str]:
    """The plist's own argv, with the secrets path and the exec'd program swapped."""
    argv = plist["ProgramArguments"]
    assert argv[:2] == ["/bin/sh", "-c"]
    script, name = argv[2], argv[3]
    return ["/bin/sh", "-c", script, name, secrets_file, *program]


# ---- the template ----------------------------------------------------------------


def test_renders_to_a_valid_plist_with_the_fleet_label():
    p = render()
    assert p["Label"] == LABEL
    assert p["RunAtLoad"] is True
    assert p["KeepAlive"] is True


def test_every_placeholder_is_filled_and_nothing_relies_on_expansion():
    """launchd expands neither `~` nor `$HOME`, which is why the template has placeholders."""
    raw = subprocess.run(
        [str(INSTALLER), "render", "--home", "/Users/tester"], check=True, capture_output=True
    ).stdout.decode()
    assert "@HOME@" not in raw and "@EXEC@" not in raw
    p = plistlib.loads(raw.encode())
    for path in (p["StandardOutPath"], p["StandardErrorPath"], *p["ProgramArguments"]):
        assert not path.startswith("~"), path
        assert "$HOME" not in path, path


def test_logs_go_to_library_logs_kpidash():
    p = render(home="/Users/tester")
    assert p["StandardOutPath"] == "/Users/tester/Library/Logs/kpidash/client.log"
    assert p["StandardErrorPath"] == p["StandardOutPath"]


def test_default_exec_is_the_published_package_under_home():
    argv = render(home="/Users/tester")["ProgramArguments"]
    assert argv[4] == SECRETS
    assert argv[5:] == [
        "/Users/tester/.local/bin/kpidash-client",
        "daemon",
        "start",
        "--foreground",
    ]


def test_exec_override():
    argv = render(exec_path="/opt/kc/bin/kpidash-client")["ProgramArguments"]
    assert argv[5] == "/opt/kc/bin/kpidash-client"


def test_the_password_is_read_from_the_one_per_host_file_and_never_stored():
    raw = subprocess.run(
        [str(INSTALLER), "render", "--home", "/Users/tester"], check=True, capture_output=True
    ).stdout.decode()
    assert SECRETS in raw
    assert "redis-auth.env" not in raw
    assert "EnvironmentVariables" not in raw  # the one place a value could be pasted in


# ---- the launcher ----------------------------------------------------------------

PRINT_AUTH = ["/bin/sh", "-c", 'printf "%s" "$REDISCLI_AUTH"']


def run_launcher(tmp_path, contents: str | None, program=PRINT_AUTH, mode=0o640):
    f = tmp_path / "secrets.env"
    if contents is not None:
        f.write_text(contents)
        f.chmod(mode)
    env = {k: v for k, v in os.environ.items() if k != "REDISCLI_AUTH"}
    return subprocess.run(
        launcher_argv(render(), str(f), program), capture_output=True, text=True, env=env
    )


def test_launcher_exports_a_single_quoted_value_verbatim(tmp_path):
    """k-homelab writes KEY='value'; a $ or ` inside must survive, never be evaluated."""
    r = run_launcher(tmp_path, "OTHER='x'\nREDISCLI_AUTH='s3$cr`et'\n")
    assert r.returncode == 0, r.stdout + r.stderr
    assert r.stdout == "s3$cr`et"


def test_launcher_exports_a_bare_value(tmp_path):
    r = run_launcher(tmp_path, "REDISCLI_AUTH=plain\n")
    assert r.returncode == 0
    assert r.stdout == "plain"


def test_launcher_passes_the_remaining_arguments_through(tmp_path):
    r = run_launcher(tmp_path, "REDISCLI_AUTH=x\n", ["/bin/echo", "daemon", "start"])
    assert r.stdout.strip() == "daemon start"


@pytest.mark.parametrize(
    "contents",
    [
        pytest.param(None, id="file missing"),
        pytest.param("OTHER='x'\n", id="no REDISCLI_AUTH line"),
        pytest.param("REDISCLI_AUTH=''\n", id="empty value"),
    ],
)
def test_launcher_refuses_to_start_without_a_password(tmp_path, contents):
    """The launchd twin of the systemd unit's un-prefixed EnvironmentFile=.

    A client with no password connects, fails every write with NOAUTH, and looks
    healthy doing it. Not starting is the louder failure.
    """
    r = run_launcher(tmp_path, contents, ["/bin/echo", "SHOULD-NOT-RUN"])
    assert r.returncode != 0
    assert "SHOULD-NOT-RUN" not in r.stdout
    assert "REDISCLI_AUTH" in r.stdout + r.stderr


def test_launcher_never_prints_the_value(tmp_path):
    r = run_launcher(tmp_path, "REDISCLI_AUTH='hunter2'\n", ["/bin/true"])
    assert "hunter2" not in r.stdout + r.stderr


# ---- the installer off macOS -----------------------------------------------------


@pytest.mark.skipif(os.uname().sysname == "Darwin", reason="asserts the non-macOS refusal")
@pytest.mark.parametrize("action", ["install", "status", "uninstall"])
def test_installer_refuses_everything_but_render_off_macos(action):
    r = subprocess.run([str(INSTALLER), action], capture_output=True, text=True)
    assert r.returncode != 0
    assert "macOS" in r.stderr
