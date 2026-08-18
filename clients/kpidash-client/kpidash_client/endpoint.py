"""endpoint.py — where Redis lives, resolved through khlenv.

khlenv (`docs/design.md` in the khlenv repo) is the homelab's config
resolver: ask it for a key and it walks `KEY.<host>.<app>` ->
`KEY.<host>` -> `KEY`, deriving `<host>` from the source address. Asking
it for the Redis endpoint is what makes moving Redis one store edit
instead of a config edit on every reporting host.

`[redis] host` in config.toml stays an **explicit override** so every
existing install keeps working untouched; drop it from config.toml and
the endpoint comes from khlenv instead.

Only the endpoint is resolved here. The password stays in
`REDISCLI_AUTH` — khlenv never holds secrets.
"""

from __future__ import annotations

import logging

from khlenv import Khlenv, KhlenvError

from .config import ClientConfig

KHLENV_APP = "kpidash-client"
KHLENV_KEY = "KPIDASH_REDIS"

#: Used only when khlenv is unreachable *and* has never been reached, so
#: there is no cached answer either. The dashboard's Redis has lived here
#: since the beginning: falling back to it can never be worse than not
#: having khlenv at all.
DEFAULT_ENDPOINT = "rpi53:6379"

DEFAULT_PORT = 6379

logger = logging.getLogger(__name__)


class EndpointError(Exception):
    """Raised when no usable Redis endpoint could be determined."""


def parse_endpoint(value: str) -> tuple[str, int]:
    """Parse `"host"` or `"host:port"` into its parts."""
    text = value.strip()
    if not text:
        raise EndpointError("Redis endpoint is empty")

    host, separator, port_text = text.rpartition(":")
    if not separator:
        return text, DEFAULT_PORT
    if not host:
        raise EndpointError(f"malformed Redis endpoint {value!r} — expected host or host:port")
    try:
        port = int(port_text)
    except ValueError:
        raise EndpointError(
            f"malformed Redis endpoint {value!r} — port {port_text!r} is not a number"
        ) from None
    if not 1 <= port <= 65535:
        raise EndpointError(f"malformed Redis endpoint {value!r} — port {port} is out of range")
    return host, port


def resolve_redis_endpoint(config: ClientConfig) -> tuple[str, int]:
    """Return the (host, port) to connect to, preferring the local override.

    Called on every connect, which is what makes khlenv's propagation path
    work: when Redis stops answering, the daemon reconnects, and the
    reconnect re-asks khlenv in case the service moved.
    """
    if config.redis_host:
        return config.redis_host, config.redis_port or DEFAULT_PORT

    try:
        value = Khlenv(KHLENV_APP).get(KHLENV_KEY, default=DEFAULT_ENDPOINT)
    except KhlenvError as exc:
        raise EndpointError(f"could not resolve {KHLENV_KEY} through khlenv: {exc}") from exc

    if value is None:
        raise EndpointError(
            f"khlenv holds an explicit null for {KHLENV_KEY}, which means "
            "'intentionally no Redis endpoint' — there is nothing to connect to. "
            "Set a value in the khlenv store, or put [redis] host in config.toml."
        )

    host, port = parse_endpoint(value)
    logger.debug("resolved Redis endpoint %s:%d from khlenv (%s)", host, port, KHLENV_KEY)
    return host, port
