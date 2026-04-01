#ifndef REDIS_H
#define REDIS_H

#include <stdbool.h>
#include "registry.h"

/**
 * Connect to Redis with optional password (pass NULL if none).
 * Stores host/port/auth for reconnection.
 * Returns true on success.
 */
bool redis_connect(const char *host, int port, const char *auth);

/**
 * Disconnect and free the hiredis context.
 */
void redis_disconnect(void);

/**
 * Returns true if the current Redis context is valid.
 */
bool redis_is_connected(void);

/**
 * Check if connection is live; reconnect if not.
 * Calls ui_show_redis_error() / ui_hide_redis_error() as needed.
 * Returns true if connection is live after the call.
 */
bool redis_reconnect_if_needed(void);

/**
 * Run one full poll cycle (called every 1s from LVGL timer).
 * Updates registry, activities, repo status, fortune pushed, status ack.
 * Assumes redis_reconnect_if_needed() was checked before calling.
 */
void redis_poll(void);

/**
 * Write kpidash:system:logpath and kpidash:system:version on startup.
 */
void redis_write_system_info(const char *logpath, const char *version);

/**
 * Write current fortune text to kpidash:fortune:current (no TTL).
 */
void redis_write_fortune_current(const char *json);

/**
 * Write "1" to kpidash:system:ping, read it back, and return true if the
 * round-trip succeeds. Prints INFO/ERROR line to stdout.
 */
bool redis_roundtrip_check(void);

/* ---- Internal JSON parse helpers exposed for unit tests ---- */

/**
 * Parse a health JSON string into the given client_info_t slot.
 * Returns true if JSON was valid and fields were populated.
 * Sets c->online = true and populates last_seen_ts, uptime_seconds.
 */
bool redis_parse_health_json(const char *json, client_info_t *c);

/**
 * Parse a telemetry JSON string into the given client_info_t slot.
 * Returns true if JSON was valid.
 */
bool redis_parse_telemetry_json(const char *json, client_info_t *c);

#endif /* REDIS_H */
