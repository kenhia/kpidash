"""
tools.py — MCP tool definitions for kpidash (T044)

Tools are registered on a FastMCP instance that server.py imports and runs.
Exceptions raised from tool functions are automatically converted to
isError=true MCP content by the FastMCP framework.
"""

from __future__ import annotations

from mcp.server.fastmcp import FastMCP

from .redis_client import MCPRedisClient, MCPRedisError

mcp = FastMCP("kpidash")


@mcp.tool()
def start_activity(name: str, host: str | None = None) -> dict:
    """
    Start a named activity on the kpidash dashboard.

    Creates an entry in Redis that appears as an active row on the
    dashboard's Activities widget. Returns the activity_id needed to
    call end_activity later.

    Args:
        name: Human-readable description of the activity (max 127 chars).
        host: Identifier for the agent or machine (max 63 chars).
              Defaults to the MCP server's hostname.
    """
    try:
        client = MCPRedisClient()
        return client.start_activity(name=name, host=host)
    except MCPRedisError as e:
        # FastMCP converts ValueError to is_error=true content
        raise ValueError(str(e)) from e


@mcp.tool()
def end_activity(activity_id: str) -> dict:
    """
    End an activity and record its completion time on the dashboard.

    Marks the activity as 'done' and records end_ts. Returns duration_seconds.

    Args:
        activity_id: The UUID returned by start_activity.
    """
    try:
        client = MCPRedisClient()
        return client.end_activity(activity_id=activity_id)
    except MCPRedisError as e:
        raise ValueError(str(e)) from e
