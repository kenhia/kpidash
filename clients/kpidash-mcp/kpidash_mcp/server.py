"""
server.py — kpidash MCP server entry point (T045)

Runs the kpidash MCP server over stdio transport (the default for local
MCP servers used by Claude Desktop and GitHub Copilot).

Usage:
    python -m kpidash_mcp.server
    # or via installed entry point:
    kpidash-mcp
"""

from __future__ import annotations

from .tools import mcp


def main() -> None:
    """Start the kpidash MCP server (stdio transport)."""
    mcp.run()


if __name__ == "__main__":
    main()
