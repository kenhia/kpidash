"""
cli.py — Click CLI entry point for kpidash-client.

Implements T035 (daemon) and T051 (status ack/log-path). The `activity` and
`fortune` groups were removed in 2.0.0 (sprint 036): nothing has read their keys
since sprint 032.
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


def _pid_path() -> Path:
    """Path to daemon.pid."""
    if platform.system() == "Windows":
        appdata = os.environ.get("APPDATA", str(Path.home() / "AppData" / "Roaming"))
        return Path(appdata) / "kpidash-client" / "daemon.pid"
    return Path.home() / ".config" / "kpidash-client" / "daemon.pid"


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
    """kpidash client — telemetry daemon and dashboard utilities."""
    ctx.ensure_object(dict)
    ctx.obj["config_path"] = config_path


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
# net (WI #4061)
# ---------------------------------------------------------------------------


@main.group("net", invoke_without_command=True)
@click.pass_context
def net(ctx: click.Context) -> None:
    """Print one kdash:net record for this host, as the daemon would publish it.

    Reads no Redis and leaves the daemon's history file alone, so the 24 h
    deltas print as null — that is the honest answer for a one-shot read.
    """
    if ctx.invoked_subcommand is not None:
        return
    import time

    from .net import NetCollector, NetHistory

    cfg = _load_config(ctx.obj.get("config_path"))
    collector = NetCollector(cfg.net, history=NetHistory(None))
    collector.prime()
    time.sleep(1)
    click.echo(json.dumps(collector.collect(), indent=2))


@net.command("ack")
@click.argument("host")
@click.option(
    "--by",
    default=None,
    help="Who reviewed it: `ken`, or an agent identity such as `claude-kai` "
    "(default: the login name).",
)
@click.pass_context
def net_ack(ctx: click.Context, host: str, by: str | None) -> None:
    """Clear HOST's reviewed warning on the network card (WI #4143).

    Records HOST's current LFC and rx-error counters as a baseline; the panel
    counts only what happens after it, so a new event turns the column yellow
    again. Expires after 24 h. Never clears a red state. Runs from any host.
    """
    import getpass

    cfg = _load_config(ctx.obj.get("config_path"))
    client = _make_client(cfg)
    try:
        ack = client.ack_net(host.lower(), by or getpass.getuser())
    except RedisClientError as e:
        click.echo(f"Error: {e}", err=True)
        sys.exit(1)
    finally:
        client.disconnect()
    click.echo(
        f"Acked kdash:net:ack:{host.lower()}: lfc={ack['lfc']} rx_errors={ack['rx_errors']} "
        f"by={ack['by']} (expires in 24 h)"
    )


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


# ---------------------------------------------------------------------------
# Service status — sprint 006 / T026
# ---------------------------------------------------------------------------


@main.command("service-status")
@click.option("--name", required=True, help="Service name (key suffix).")
@click.option(
    "--state",
    required=True,
    type=click.Choice(["ok", "unhealthy", "maintenance", "down"], case_sensitive=False),
)
@click.option("--text", required=True, help="Status text shown on the card.")
@click.option(
    "--host",
    default="_",
    show_default=True,
    help="Host the service runs on. Use the default '_' for non-host-scoped services.",
)
@click.option("--icon", type=int, default=None, help="Icon index 0..15 (optional).")
@click.pass_context
def service_status(
    ctx: click.Context,
    name: str,
    state: str,
    text: str,
    host: str,
    icon: int | None,
) -> None:
    """Publish a service status payload to kpidash:services:<name>:<host>."""
    cfg = _load_config(ctx.obj.get("config_path"))
    client = _make_client(cfg)
    try:
        client.set_service_status(
            name=name, state=state.lower(), text=text, host=host, icon=icon
        )
    except RedisClientError as e:
        click.echo(f"ERROR: {e}", err=True)
        sys.exit(1)
    click.echo(f"OK: kpidash:services:{name}:{host} state={state.lower()}")
