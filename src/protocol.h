#ifndef PROTOCOL_H
#define PROTOCOL_H

/* ---- String size constants ---- */
#define HOSTNAME_LEN 64
#define GPU_NAME_LEN 64
#define LABEL_LEN 64
#define TASK_LEN 128
#define ACTIVITY_ID_LEN 37 /* UUID4 + NUL */
#define ACTIVITY_NAME_LEN 128
#define OS_NAME_LEN 128
#define MSG_BUF_SIZE 4096

/* ---- Capacity limits ---- */
#define MAX_CLIENTS 16
#define MAX_DISKS 8
#define ACTIVITY_MAX_DISPLAY 10
#define PRIORITY_CLIENTS_MAX 16

/* ---- TTL constants (seconds) ---- */
#define HEALTH_TTL_S 5
#define TELEMETRY_TTL_S 15
#define REPOS_TTL_S 30
#define STATUS_ACK_TTL_S 60

/* ---- Fortune rotation interval (seconds, compile-time constant) ---- */
#define FORTUNE_INTERVAL_S 300

/* ---- Application version ---- */
#define KPIDASH_VERSION "1.0.0"

/* ---- Redis key macros (%s = hostname or id placeholder) ---- */
#define KPIDASH_KEY_HEALTH "kpidash:client:%s:health"
#define KPIDASH_KEY_TELEMETRY "kpidash:client:%s:telemetry"
#define KPIDASH_KEY_DEV_TELEMETRY "kpidash:client:%s:dev_telemetry"
#define KPIDASH_KEY_CLIENTS "kpidash:clients"
#define KPIDASH_KEY_ACTIVITIES "kpidash:activities"
#define KPIDASH_KEY_ACTIVITY "kpidash:activity:%s"
#define KPIDASH_KEY_REPOS "kpidash:repos:%s"
#define KPIDASH_KEY_FORTUNE_CURRENT "kpidash:fortune:current"
#define KPIDASH_KEY_FORTUNE_PUSHED "kpidash:fortune:pushed"
#define KPIDASH_KEY_STATUS_CURRENT "kpidash:status:current"
#define KPIDASH_KEY_STATUS_ACK "kpidash:status:ack:%s"
#define KPIDASH_KEY_LOGPATH "kpidash:system:logpath"
#define KPIDASH_KEY_VERSION "kpidash:system:version"
#define KPIDASH_KEY_SYSTEM_MEM_CURRENT "kpidash:system:mem:current"
#define KPIDASH_KEY_SYSTEM_MEM_RING "kpidash:system:mem:ring"
/* Ring keeps ~25h of 60s samples => 1500 entries (LTRIM 0 1499). */
#define KPIDASH_MEM_RING_MAX 1500

/* ---- Development command keys (002-exploration-sprint) ---- */
#define KPIDASH_KEY_CMD_GRID         "kpidash:cmd:grid"
#define KPIDASH_KEY_CMD_TEXTSIZE     "kpidash:cmd:textsize"
#define KPIDASH_KEY_CMD_GRAPH        "kpidash:cmd:graph"
#define KPIDASH_KEY_CMD_GRAPH_HOST   "kpidash:cmd:graph:%s"
#define KPIDASH_KEY_CMD_FORTUNE_DEV  "kpidash:cmd:fortune_dev"

/* WI #374: card eviction command — dashboard GETDELs this each poll and drops
 * the named cards from its in-memory registries. Value: JSON array of targets
 * [{"kind":"service","name":..,"host":..}, {"kind":"apttemps","name":<slug>}]. */
#define KPIDASH_KEY_CMD_SERVICES_EVICT "kpidash:cmd:services:evict"

/* One-shot device self-screenshot trigger. Consumed with GETDEL each poll;
 * a value starting with '/' names the output path, else the default is used. */
#define KPIDASH_KEY_SCREENSHOT "kpidash:screenshot"

/* Sprint 006 / FR-021: service status keys (one per service). */
#define KPIDASH_KEY_SERVICES_PATTERN "kpidash:services:*:*"
#define KPIDASH_KEY_SERVICES_PREFIX  "kpidash:services:"

/* WI #364: per-zone apartment temperature cards (one key per zone). */
#define KPIDASH_KEY_APTTEMPS_PATTERN "kpidash:apttemps:*"
#define KPIDASH_KEY_APTTEMPS_PREFIX  "kpidash:apttemps:"

/* WI #1903: per-deployer host staleness flags, written by the fleet deployers
 * (agent-skills' fleet-deploy, k-homelab's apply/audit) through kdash-pub.
 *
 * Key: kdash:stale:<host>:<deployer>   Value: {"stale":true,"since":<unix s>,
 *                                              "reason":"<one line>","ts":<unix s>}
 *
 * The feed is PRESENCE-OWNED (kdashdata CD-18): there is no TTL and no
 * staleness window — the key existing IS the flag, and its absence is the only
 * all-clear. A deployer that never runs again correctly leaves the flag up.
 * The payload only ENRICHES a signal the key has already given, which inverts
 * this codebase's usual skip-the-unparseable-record rule: dropping an
 * unreadable staleness record would render as all-clear. See redis_poll_stale.
 *
 * The pattern is deliberately one segment looser than the key contract so a
 * malformed key still reaches the reader and still raises its host; the shape
 * is sorted out in redis_parse_stale_key. */
#define KDASH_KEY_STALE_PATTERN "kdash:stale:*"
#define KDASH_KEY_STALE_PREFIX  "kdash:stale:"

/* The dashboard's own host is excluded: if rpi53 is behind there is no
 * dashboard to say so, and a card about the panel you are looking at is noise
 * (WI #1903). */
#define STALE_EXCLUDED_HOST "rpi53"

/* Shown in place of a deployer name when the key is off-contract (no deployer
 * segment, or too many). The host is still raised — see CD-18 above. */
#define STALE_DEPLOYER_UNKNOWN "(unknown)"

#define CMD_TTL_S 300

#endif /* PROTOCOL_H */
