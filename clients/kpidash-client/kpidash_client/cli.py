"""
cli.py — Click CLI entry point for kpidash-client.

Implements T028 (activity), T035 (daemon), T051 (status ack/log-path), T054 (fortune push).
"""

from __future__ import annotations

import json
import os
import platform
import signal
import sys
from pathlib import Path

import click

from .config import ClientConfig, ConfigError
from .redis_client import RedisClient, RedisClientError

# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------


def _state_path() -> Path:
    """Path to state.json storing the last activity_id."""
    if platform.system() == "Windows":
        appdata = os.environ.get("APPDATA", str(Path.home() / "AppData" / "Roaming"))
        return Path(appdata) / "kpidash-client" / "state.json"
    return Path.home() / ".config" / "kpidash-client" / "state.json"


def _pid_path() -> Path:
    """Path to daemon.pid."""
    if platform.system() == "Windows":
        appdata = os.environ.get("APPDATA", str(Path.home() / "AppData" / "Roaming"))
        return Path(appdata) / "kpidash-client" / "daemon.pid"
    return Path.home() / ".config" / "kpidash-client" / "daemon.pid"


def _load_state() -> dict:
    p = _state_path()
    if p.exists():
        try:
            return json.loads(p.read_text())
        except (json.JSONDecodeError, OSError):
            pass
    return {}


def _save_state(state: dict) -> None:
    p = _state_path()
    p.parent.mkdir(parents=True, exist_ok=True)
    p.write_text(json.dumps(state, indent=2))


def _load_config(config_path: str | None) -> ClientConfig:
    try:
        return ClientConfig.load(Path(config_path) if config_path else None)
    except ConfigError as e:
        click.echo(f"Config error: {e}", err=True)
        sys.exit(1)


def _make_client(cfg: ClientConfig) -> RedisClient:
    client = RedisClient(cfg)
    try:
        client.connect()
    except RedisClientError as e:
        click.echo(f"Error: {e}", err=True)
        sys.exit(1)
    return client


# ---------------------------------------------------------------------------
# Root group
# ---------------------------------------------------------------------------


@click.group()
@click.option(
    "--config",
    "config_path",
    default=None,
    help="Path to config.toml (default: platform-specific XDG path)",
)
@click.pass_context
def main(ctx: click.Context, config_path: str | None) -> None:
    """kpidash client — activity tracking, telemetry daemon, and dashboard utilities."""
    ctx.ensure_object(dict)
    ctx.obj["config_path"] = config_path


# ---------------------------------------------------------------------------
# activity
# ---------------------------------------------------------------------------


@main.group()
def activity() -> None:
    """Manage activities shown on the kpidash dashboard."""


@activity.command("start")
@click.argument("name")
@click.pass_context
def activity_start(ctx: click.Context, name: str) -> None:
    """Start a named activity and record its ID locally in state.json."""
    cfg = _load_config(ctx.obj.get("config_path"))
    client = _make_client(cfg)
    try:
        activity_id = client.start_activity(name)
    except RedisClientError as e:
        click.echo(f"Error: {e}", err=True)
        sys.exit(1)
    finally:
        client.disconnect()

    state = _load_state()
    state["activity_id"] = activity_id
    _save_state(state)
    click.echo(f"Started: {name!r}  (id: {activity_id})")


@activity.command("done")
@click.argument("activity_id", required=False)
@click.pass_context
def activity_done(ctx: click.Context, activity_id: str | None) -> None:
    """Mark an activity as done.  Uses last started activity if no ID given."""
    if not activity_id:
        activity_id = _load_state().get("activity_id")
    if not activity_id:
        click.echo(
            "Error: no activity_id found.  Pass one explicitly or run `activity start` first.",
            err=True,
        )
        sys.exit(1)

    cfg = _load_config(ctx.obj.get("config_path"))
    client = _make_client(cfg)
    try:
        client.end_activity(activity_id)
    except RedisClientError as e:
        click.echo(f"Error: {e}", err=True)
        sys.exit(1)
    finally:
        client.disconnect()

    state = _load_state()
    state.pop("activity_id", None)
    _save_state(state)
    click.echo(f"Done: {activity_id}")


# ---------------------------------------------------------------------------
# status (T051)
# ---------------------------------------------------------------------------


@main.group()
def status() -> None:
    """Inspect and acknowledge dashboard status messages."""


@status.command("ack")
@click.pass_context
def status_ack(ctx: click.Context) -> None:
    """Acknowledge the current dashboard status message."""
    cfg = _load_config(ctx.obj.get("config_path"))
    client = _make_client(cfg)
    try:
        msg = client.get_status_current()
        if not msg:
            click.echo("No pending status message.")
            return
        message_id = msg.get("message_id")
        if not message_id:
            click.echo("Error: status message has no message_id.", err=True)
            sys.exit(1)
        client.ack_status(message_id)
        click.echo(f"Acknowledged: [{msg.get('severity', '?')}] {msg.get('message', '')}")
    except RedisClientError as e:
        click.echo(f"Error: {e}", err=True)
        sys.exit(1)
    finally:
        client.disconnect()


# ---------------------------------------------------------------------------
# log-path (T051)
# ---------------------------------------------------------------------------


@main.command("log-path")
@click.pass_context
def log_path(ctx: click.Context) -> None:
    """Print the dashboard log file path stored in Redis."""
    cfg = _load_config(ctx.obj.get("config_path"))
    client = _make_client(cfg)
    try:
        path = client.get_logpath()
    except RedisClientError as e:
        click.echo(f"Error: {e}", err=True)
        sys.exit(1)
    finally:
        client.disconnect()
    if path:
        click.echo(path)
    else:
        click.echo("Log path not set (is the dashboard running?).", err=True)
        sys.exit(1)


# ---------------------------------------------------------------------------
# fortune (T054)
# ---------------------------------------------------------------------------


@main.group()
def fortune() -> None:
    """Push fortune messages to the dashboard display."""


@fortune.command("push")
@click.argument("text")
@click.option(
    "--duration",
    default=300,
    show_default=True,
    help="How long to display the fortune (seconds)",
)
@click.pass_context
def fortune_push(ctx: click.Context, text: str, duration: int) -> None:
    """Push TEXT to the dashboard fortune display for DURATION seconds."""
    cfg = _load_config(ctx.obj.get("config_path"))
    client = _make_client(cfg)
    try:
        client.push_fortune(text, duration)
    except RedisClientError as e:
        click.echo(f"Error: {e}", err=True)
        sys.exit(1)
    finally:
        client.disconnect()
    click.echo(f"Fortune pushed (visible for {duration}s).")


# ---------------------------------------------------------------------------
# daemon (T035)
# ---------------------------------------------------------------------------


@main.group()
def daemon() -> None:
    """Start and stop the background telemetry/health daemon."""


@daemon.command("start")
@click.option(
    "--foreground",
    is_flag=True,
    default=False,
    help="Run in the foreground (required on Windows; useful for debugging)",
)
@click.pass_context
def daemon_start(ctx: click.Context, foreground: bool) -> None:
    """Start the kpidash client daemon."""
    from .daemon import run_daemon  # noqa: PLC0415 — avoid circular at module level

    cfg = _load_config(ctx.obj.get("config_path"))

    if foreground or platform.system() == "Windows":
        # Windows: no fork(); run in-process
        run_daemon(cfg)
        return

    # Unix: double-fork daemonise
    pid = os.fork()
    if pid == 0:
        # Child: create new session, redirect stdio, run daemon
        os.setsid()
        devnull_fd = os.open(os.devnull, os.O_RDWR)
        for fd in (0, 1, 2):
            os.dup2(devnull_fd, fd)
        run_daemon(cfg)
        sys.exit(0)
    else:
        # Parent: record PID and exit
        pid_file = _pid_path()
        pid_file.parent.mkdir(parents=True, exist_ok=True)
        pid_file.write_text(str(pid))
        click.echo(f"Daemon started (pid {pid}). PID file: {pid_file}")


@daemon.command("stop")
def daemon_stop() -> None:
    """Stop the running daemon."""
    pid_file = _pid_path()
    if not pid_file.exists():
        click.echo("No PID file found; is the daemon running?", err=True)
        sys.exit(1)
    try:
        pid = int(pid_file.read_text().strip())
        os.kill(pid, signal.SIGTERM)
        pid_file.unlink(missing_ok=True)
        click.echo(f"Sent SIGTERM to pid {pid}.")
    except ValueError:
        click.echo("PID file is corrupted.", err=True)
        pid_file.unlink(missing_ok=True)
        sys.exit(1)
    except ProcessLookupError:
        click.echo("Process not found; cleaning up PID file.", err=True)
        pid_file.unlink(missing_ok=True)
        sys.exit(1)
