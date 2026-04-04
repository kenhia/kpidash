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
 * Return the current in-memory activity list (populated by redis_poll).
 * *count is set to the number of valid entries.
 */
const activity_t *redis_get_activities(int *count);

/**
 * Return the current in-memory repo list (populated by redis_poll).
 * *count is set to the number of valid entries.
 */
const repo_entry_t *redis_get_repos(int *count);

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

/**
 * Parse kpidash:cmd:grid JSON into state.
 * json == NULL (key absent) → grid_enabled=false, grid_size=0.
 * Returns true if json was valid or NULL; false only on malformed JSON.
 */
bool redis_parse_cmd_grid_json(const char *json, dev_cmd_state_t *state);

/**
 * Parse kpidash:cmd:textsize JSON into state.
 * json == NULL (key absent) → textsize_enabled=false.
 */
bool redis_parse_cmd_textsize_json(const char *json, dev_cmd_state_t *state);

/**
 * Parse kpidash:cmd:graph JSON into state.
 * json == NULL (key absent) → graph_enabled=false.
 * JSON: {"enabled": true, "client": "hostname"}
 */
bool redis_parse_cmd_graph_json(const char *json, dev_cmd_state_t *state);

/**
 * Return a pointer to the current dev command state (updated each poll cycle).
 * Not thread-safe for writing; read from LVGL timer (main thread) only.
 */
const dev_cmd_state_t *redis_get_dev_cmd_state(void);

#endif /* REDIS_H */
