#ifndef REGISTRY_H
#define REGISTRY_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <time.h>

#include "protocol.h"

/* ---- Disk types ---- */
typedef enum { DISK_NVME, DISK_SSD, DISK_HDD, DISK_OTHER } disk_type_t;

typedef struct {
    char label[LABEL_LEN];
    disk_type_t type;
    float used_gb;
    float total_gb;
    float pct;
} disk_entry_t;

/* ---- GPU info (optional) ---- */
typedef struct {
    bool present;
    char name[GPU_NAME_LEN];
    uint32_t vram_used_mb;
    uint32_t vram_total_mb;
    float compute_pct;
} gpu_info_t;

#ifndef KPIDASH_TEST_STUBS
#include "lvgl.h"
#endif

/* ---- Per-client data (populated by redis.c on each poll) ---- */
typedef struct {
    char hostname[HOSTNAME_LEN];
    bool online;         /* health key present in Redis */
    double last_seen_ts; /* Unix epoch of last health ping */
    float uptime_seconds;
    char os_name[OS_NAME_LEN]; /* e.g. "Linux 5.15.0-173-generic" */
    float cpu_pct;
    float top_core_pct;
    uint32_t ram_used_mb;
    uint32_t ram_total_mb;
    gpu_info_t gpu;
    disk_entry_t disks[MAX_DISKS];
    int disk_count;
    double telemetry_ts; /* timestamp from last telemetry write */

#ifndef KPIDASH_TEST_STUBS
    /* LVGL widget handles — owned by ui.c, NULL until card is created */
    lv_obj_t *container;
    lv_obj_t *status_led;
    lv_obj_t *hostname_label;
    lv_obj_t *task_label;
    lv_obj_t *elapsed_label;
#endif

    /* Task tracking (for elapsed timers, even in test builds) */
    bool active;                /* a task is currently running */
    char task[128];             /* current activity name */
    char last_task[128];        /* last completed activity name */
    double task_start;          /* Unix epoch of current task start */
    double last_task_completed; /* Unix epoch of last completion */
    double last_health;         /* Unix epoch of last health packet (for UI timeout) */
} client_info_t;

/* ---- Activity (global widget, not per-client) ---- */
typedef struct {
    char activity_id[ACTIVITY_ID_LEN];
    char host[HOSTNAME_LEN];
    char name[ACTIVITY_NAME_LEN];
    bool is_done;
    double start_ts;
    double end_ts; /* 0 if still active */
} activity_t;

/* ---- Repo status entry ---- */
typedef struct {
    char host[HOSTNAME_LEN];
    char name[LABEL_LEN];
    char path[256];
    char branch[128];
    char default_branch[128];
    bool is_dirty;
    bool detached_head;
    int ahead;
    int behind;
    int untracked_count;
    int changed_count;
    int deleted_count;
    int renamed_count;
    double last_commit_ts;
    int sort_order; /* 0 = explicit, 1 = scanned; lower = higher priority */
    double ts;
} repo_entry_t;

/* ---- Development command state (transient, from Redis poll) ---- */
typedef struct {
    bool grid_enabled;
    int grid_size;        /* pixels; 0 = disabled */
    bool grid_unit;       /* unit-based grid mode */
    float grid_unit_size; /* unit multiplier (0.5, 1, 2) */
    bool textsize_enabled;
    bool graph_enabled; /* global switch: false hides all graphs */
    int graph_host_count;
    char graph_hosts[MAX_CLIENTS][HOSTNAME_LEN];
    bool graph_host_enabled[MAX_CLIENTS];
    bool fortune_dev_enabled;        /* show reject/elapsed overlay on fortune widget */
} dev_cmd_state_t;

/* ---- Dev telemetry snapshot (fast GPU+CPU+RAM from dev_telemetry key) ---- */
typedef struct {
    bool valid; /* true if key was present and parsed */
    char host[HOSTNAME_LEN]; /* sprint 006/T034: source host; "(legacy)" if absent */
    float cpu_pct;
    float top_core_pct;
    uint32_t ram_used_mb;
    uint32_t ram_total_mb;
    bool gpu_present;
    float gpu_compute_pct;
    uint32_t gpu_vram_used_mb;
    uint32_t gpu_vram_total_mb;
} dev_telemetry_t;

/* ---- Registry API (global singleton, thread-safe) ---- */
void registry_init(void);
void registry_lock(void);
void registry_unlock(void);

/**
 * Find existing client by hostname or allocate a new slot.
 * Must be called with the registry locked.
 * Returns NULL if the registry is full, or on a NULL/empty hostname.
 *
 * Prefer registry_admit() from the poll loop — see WI #2524.
 */
client_info_t *registry_find_or_create(const char *hostname);

/**
 * Find an existing client by hostname. NEVER allocates.
 * Must be called with the registry locked.
 * Returns NULL if this host has no card.
 */
client_info_t *registry_find(const char *hostname);

/**
 * Admit a `kpidash:clients` member to the card grid (WI #2524).
 *
 * `has_client_data` says whether THIS poll cycle found any
 * `kpidash:client:<host>:*` payload for the member. The rule is
 * admission-on-data:
 *
 *   - a member that has never published gets no slot, because the set is
 *     append-only (redis_client.py SADDs, nothing ever SREMs) and
 *     membership alone therefore proves nothing about the host existing;
 *   - a member that HAS been admitted keeps its slot when the data stops,
 *     so a real outage still renders as a down card rather than as a host
 *     that quietly vanishes. Every client key is TTL'd (health 5 s,
 *     telemetry 15 s), so "no data this cycle" is any host fifteen seconds
 *     dead — dropping on that would be the bug, not the fix.
 *
 * A host admitted in an EARLIER dashboard run counts as admitted: the set is
 * seeded from disk at startup by registry_seed_admitted(), which is what
 * stops a host that is down across a restart losing its card (WI #3012).
 *
 * Must be called with the registry locked. Returns NULL when the member is
 * not (yet) admitted, and NULL is a normal answer, not an error.
 */
client_info_t *registry_admit(const char *hostname, bool has_client_data);

/**
 * Seed the admitted set from the admitted-hosts file (WI #3012).
 *
 * Call once at startup, after registry_init() and before the first poll.
 * Invalid and duplicate names are skipped; the set is bounded by
 * MAX_CLIENTS. Seeding does NOT mark the set dirty — it came from the file,
 * so writing it straight back would be a pointless disk write on every boot.
 *
 * Returns the number of hostnames accepted.
 */
int registry_seed_admitted(const char hosts[][HOSTNAME_LEN], int count);

/**
 * Copy the admitted hostnames into out[]. Returns the number copied.
 * Thread-safe; takes the lock itself.
 */
int registry_admitted_snapshot(char out[][HOSTNAME_LEN], int max);

/**
 * True if the admitted set has grown since the last call, and CLEARS the
 * flag. This is the "write it only when the set changes" signal — the caller
 * saves the file exactly when this returns true. Thread-safe.
 */
bool registry_admitted_take_dirty(void);

/**
 * Remove clients whose hostnames are NOT in the provided set.
 * Call with registry locked.
 */
void registry_remove_absent(const char **hostnames, int count);

/**
 * Copy all client_info_t entries into out[].
 * Returns number of clients copied. Thread-safe.
 */
int registry_snapshot(client_info_t *out, int max_count);

/**
 * Returns the total number of tracked clients.
 */
int registry_count(void);

/**
 * Configure the list of priority clients (comma-parsed by config.c).
 * Priority clients are placed first in the card grid and are never
 * evicted to make room for non-priority clients (T056).
 * Call once after config_load(), before the poll loop.
 * Must NOT be called with the registry locked.
 */
void registry_set_priority_clients(const char hosts[][HOSTNAME_LEN], int count);

/**
 * Return the priority index (0-based) for the given hostname,
 * or -1 if the hostname is not in the priority list.
 * Thread-safe (reads only; list is set once at startup).
 */
int registry_priority_index(const char *hostname);

/* ============================================================
 * Sprint 006 additions
 * ============================================================ */

/* ---- Service registry (T007/T008) ---- */
typedef enum {
    SERVICE_STATE_UNKNOWN = 0,
    SERVICE_STATE_OK,
    SERVICE_STATE_UNHEALTHY,
    SERVICE_STATE_MAINTENANCE,
    SERVICE_STATE_DOWN,
} service_state_t;

typedef enum {
    SERVICE_COLOR_GRAY = 0,   /* DOWN sticky, or UNKNOWN */
    SERVICE_COLOR_GREEN,      /* fresh OK */
    SERVICE_COLOR_YELLOW,     /* fresh UNHEALTHY */
    SERVICE_COLOR_BLUE,       /* fresh MAINTENANCE */
    SERVICE_COLOR_RED,        /* stale non-DOWN (was OK/UNHEALTHY/MAINTENANCE, ts older than SERVICE_FRESH_SECONDS) */
} service_color_t;

/* Freshness window for service status cards. A card stays coloured as long as
 * its payload is younger than this; older payloads go RED (non-DOWN).
 *
 * This is the *consumer-side* staleness gate and is intentionally larger than a
 * publisher's nominal update cadence (~60s) to absorb scheduler drift, scan
 * jitter, and Redis round-trips — i.e. 60s nominal + 15s grace. Setting it equal
 * to the publish interval is fragile: any drift trips a false RED. Publishers
 * standardise on ~60s updates and rely on this grace. */
#define SERVICE_FRESH_SECONDS 75.0

/* WI #902: the window for a feed that publishes DAILY rather than every ~60s.
 *
 * 26 hours, and the two extra hours are load-bearing: one absorbs a
 * daylight-savings shift, one absorbs timer jitter, on top of a ~24h refresh.
 * kmon's own timer is `OnCalendar=*-*-* 11:00:00 UTC` — UTC-pinned, so DST
 * never moves it and 25h would do for kmon alone — but the Service Card is a
 * reusable type and any future consumer scheduled in LOCAL wall-clock time
 * eats the DST hour twice a year. The hour is for the general case, not the
 * first one. (Ken, WI #902: the cost of an hour of extra tolerance is nearly
 * nothing; the cost of a false RED at 2am on the Sunday the clocks change is a
 * card nobody trusts.)
 *
 * Do NOT widen this casually. The card exists because kmon died silently for
 * 10 days (2026-07-23 -> 2026-08-01) while the dashboard kept showing a stale
 * GREEN "OK" from before the break; the whole point is that ONE skipped day is
 * visible. */
#define SERVICE_DAILY_FRESH_SECONDS 93600.0 /* 26 h */

/* The freshness window that applies to `name`, in seconds.
 *
 * Per-service rather than per-payload on purpose (WI #902): the publisher
 * contract in docs/CLIENT-PROTOCOL.md §8a carries no cadence field, and
 * inventing one would fork a card shape that now has two consumers. The window
 * is a rendering policy the dashboard owns, so it stays on this side. Returns
 * SERVICE_FRESH_SECONDS for anything not known to be a daily feed. */
double service_fresh_window(const char *name);

typedef struct service_entry {
    char name[64];           /* from key suffix (kpidash:services:<name>) */
    char host[64];           /* optional, "" if absent */
    char text[128];          /* last valid status text */
    int  icon_index;         /* -1 if absent or unknown */
    double last_payload_ts;  /* payload's own ts */
    service_state_t last_valid_state;

#ifndef KPIDASH_TEST_STUBS
    lv_obj_t *container;
    lv_obj_t *border;
    lv_obj_t *icon_label;
    lv_obj_t *name_label;
    lv_obj_t *host_label;
    lv_obj_t *text_label;
#else
    void *container;
    void *border;
    void *icon_label;
    void *name_label;
    void *host_label;
    void *text_label;
#endif
} service_entry_t;

/* Parse a service state string. Returns SERVICE_STATE_UNKNOWN for
 * NULL, empty, or unrecognised input. */
service_state_t service_parse_state(const char *s);

/* Compute the card border colour per data-model.md §2 state table.
 * DOWN is sticky-gray (freshness ignored). OK/UNHEALTHY/MAINTENANCE map
 * to their colour iff (now - last_payload_ts) < SERVICE_FRESH_SECONDS, else
 * RED. UNKNOWN returns GRAY (caller is responsible for not rendering UNKNOWN
 * cards). */
service_color_t service_color(const service_entry_t *e, double now);

/* ---- Service registry (T022) ---- */
#define SERVICE_REGISTRY_MAX 32

/* Find an existing service entry by (name, host), or allocate a new slot.
 * (name, host) together form the identity. Returns NULL if the table is
 * full or inputs are NULL/empty. Thread-safe. The `host` may be the
 * sentinel "_" indicating a non-host-scoped service. */
service_entry_t *service_registry_find_or_create(const char *name, const char *host);

/* Apply a parsed-OK payload to an existing entry. Updates
 * last_valid_state, last_payload_ts, text, icon_index. Identity fields
 * (name, host) are NOT modified — they were set at create time from the
 * Redis key segments, which are authoritative. Caller MUST only invoke
 * this with payloads that pass redis_parse_service_payload (so the
 * FR-022a malformed-ignore rule is preserved). */
void service_registry_apply_payload(service_entry_t *e, const service_entry_t *parsed);

/* Snapshot all current entries into out[]; returns count written. */
int service_registry_snapshot(service_entry_t *out, int max);

/* ---- Apt-Temps registry (WI #364) ---- */
#define APTTEMPS_REGISTRY_MAX 16
/* Payloads older than this render GRAY (stale); the publisher owns freshness. */
#define APTTEMPS_FRESH_SECONDS 300.0

/* The PALETTE, and only the palette (WI #2244). The band edges quoted against
 * each colour are libkdash's KDASH_APTTEMPS_{COLD,OK,HOT}_F defaults, restated
 * here for the reader — they are no longer defined in this repo, and
 * kdash_apttemps_band() is the one place that decides which band a reading is
 * in. Changing a number here changes nothing; change it in kdashdata. */
typedef enum {
    APTTEMPS_COLOR_GRAY = 0,  /* KDASH_TEMP_STALE — stale or never-valid */
    APTTEMPS_COLOR_BLUE,      /* KDASH_TEMP_COLD  — below 65.0 */
    APTTEMPS_COLOR_GREEN,     /* KDASH_TEMP_OK    — 65.0 through 75.0 */
    APTTEMPS_COLOR_ORANGE,    /* KDASH_TEMP_WARM  — above 75.0, below 80.0 */
    APTTEMPS_COLOR_RED,       /* KDASH_TEMP_HOT   — 80.0 and above */
} apttemps_color_t;

typedef struct apttemps_entry {
    char slug[64];          /* key suffix (lowercase) — identity */
    char zone[64];          /* display label from payload "zone" */
    float temp_f;
    int  humidity_pct;
    double last_payload_ts;
    bool valid;
#ifndef KPIDASH_TEST_STUBS
    lv_obj_t *container;
    lv_obj_t *zone_label;
    lv_obj_t *temp_label;
    lv_obj_t *humidity_label;
#else
    void *container;
    void *zone_label;
    void *temp_label;
    void *humidity_label;
#endif
} apttemps_entry_t;

/* Temperature-band colour; GRAY when stale (>APTTEMPS_FRESH_SECONDS) or invalid. */
apttemps_color_t apttemps_color(const apttemps_entry_t *e, double now);

/* Find by slug or allocate a new slot. Returns NULL if full/empty. Thread-safe. */
apttemps_entry_t *apttemps_registry_find_or_create(const char *slug);
/* Apply a parsed payload (temp/humidity/ts/zone); identity slug is pinned. */
void apttemps_registry_apply_payload(apttemps_entry_t *e, const apttemps_entry_t *parsed);
/* Snapshot all entries into out[]; returns count written. */
int apttemps_registry_snapshot(apttemps_entry_t *out, int max);

/* WI #374: remove a card entry by identity and return its card container
 * (for the caller to destroy on the LVGL thread), or NULL if not found.
 * Returned as void* so registry.h stays LVGL-agnostic under test stubs. */
void *service_registry_remove(const char *name, const char *host);
void *apttemps_registry_remove(const char *slug);

/* ============================================================
 * WI #1903: host staleness registry (kdash:stale:<host>:<deployer>)
 * ============================================================
 *
 * One entry per stale HOST; the deployers that flagged it are the card body.
 * Unlike the service and apt-temps registries this one is rebuilt from the
 * feed every poll, because the feed is presence-owned (CD-18): a key that
 * stops existing is the ONLY all-clear signal, so an entry that is not
 * observed in a completed scan must go.
 *
 * That makes the cycle protocol load-bearing. begin/observe/commit replaces
 * the live set; abort discards the pending set and leaves the live one
 * standing. A scan that failed half way MUST abort rather than commit — a
 * partial scan committed would drop hosts nothing had cleared, and an
 * unraised card reads as "everything is fine", which is the one wrong answer
 * this feed exists to prevent. */
#define STALE_REGISTRY_MAX 8    /* hosts shown at once */
#define STALE_DEPLOYERS_MAX 8   /* deployers per host */

typedef struct {
    char name[64];      /* deployer, from the key's 4th segment */
    double since;       /* payload's `since`; meaningless unless since_known */
    bool since_known;   /* false when the payload was absent or unreadable */
} stale_deployer_t;

typedef struct stale_entry {
    char host[64];      /* identity, from the key's 3rd segment */
    stale_deployer_t deployers[STALE_DEPLOYERS_MAX];
    int deployer_count;
#ifndef KPIDASH_TEST_STUBS
    lv_obj_t *container;
    lv_obj_t *title_label;
    lv_obj_t *body_label;
#else
    void *container;
    void *title_label;
    void *body_label;
#endif
} stale_entry_t;

/* Start a new scan cycle: clears the pending set. The live set is untouched
 * until commit, so the card row keeps rendering the previous cycle meanwhile. */
void stale_registry_begin_cycle(void);

/* Record one observed key. `host` is required; `deployer` may be
 * STALE_DEPLOYER_UNKNOWN. `since_known` false means the payload could not be
 * read — the host is still raised, only the detail is dropped (CD-18).
 * Silently ignores STALE_EXCLUDED_HOST and anything over the capacity limits. */
void stale_registry_observe(const char *host, const char *deployer, double since,
                            bool since_known);

/* Promote the pending set to live. Deployers within each host are sorted
 * oldest `since` first (WI #1903); a deployer whose `since` is unknown carries
 * no ordering information and sorts last, then by name so the order is stable.
 * Hosts are sorted by name. Entries that were live and are not in the pending
 * set survive as zero-deployer entries until stale_registry_reap collects
 * them, so their LVGL cards can be destroyed on the LVGL thread. */
void stale_registry_commit_cycle(void);

/* Discard the pending set; the live set is unchanged. Call this instead of
 * commit whenever the scan did not complete. */
void stale_registry_abort_cycle(void);

/* Snapshot the live entries (including any awaiting reap). */
int stale_registry_snapshot(stale_entry_t *out, int max);

/* Look up a live entry by host, without creating one. NULL if absent.
 * ui.c uses this to hang the card's LVGL handles on the live entry. */
stale_entry_t *stale_registry_find(const char *host);

/* Remove every live entry with no deployers, writing each one's card
 * container into out[] for the caller to destroy. Returns the count written.
 * Returned as void* so registry.h stays LVGL-agnostic under test stubs. */
int stale_registry_reap(void **out_containers, int max);

/* Card title: "<host> stale". */
void stale_format_title(const stale_entry_t *e, char *buf, size_t n);

/* Card body: deployer names, one per line, in entry order (which commit has
 * already sorted oldest-first). */
void stale_format_body(const stale_entry_t *e, char *buf, size_t n);

/* ---- Graph host series (T006) ---- */
#define GRAPH_HOST_MAX 8
#define GRAPH_HOST_STALE_SECONDS 30.0
/* WI #250: a series stale beyond this is evicted entirely (widget destroyed)
 * so dead/legacy hosts stop lingering as "NO NEW DATA" until a restart. */
#define GRAPH_HOST_EVICT_SECONDS 300.0

typedef struct {
    char host[64];
    double last_sample_ts;
#ifndef KPIDASH_TEST_STUBS
    lv_obj_t *widget;
#else
    void *widget;
#endif
    bool stale;
    /* T035: per-host telemetry snapshot, updated by redis_poll Step 8 each
     * cycle so the UI can render one dev_graph per host without consulting
     * the legacy single-host g_dev_telemetry global. */
    dev_telemetry_t telemetry;
} graph_host_series_t;

/* Lookup an existing host series, or allocate a new slot.
 * Returns NULL if the table is full. */
graph_host_series_t *graph_host_find_or_create(const char *host);

/* Update last_sample_ts for the named host (no-op if not present). */
void graph_host_touch(const char *host, double ts);

/* Is this series considered stale? (now - last_sample_ts >= 30.0 s) */
bool graph_host_is_stale(const graph_host_series_t *s, double now);

/* Snapshot of all currently allocated host series.
 * Writes up to max entries into out[] and returns the count. */
int graph_host_snapshot(graph_host_series_t *out, int max);

/* WI #250: remove a host series by name (compacts the array). Returns true if
 * one was removed. The caller MUST destroy the series's `widget` first. */
bool graph_host_remove(const char *host);

/* ---- Layout pool (T004/T005) ---- */
typedef enum {
    WIDGET_GRAPH = 0,        /* highest priority; one per graph_host_series */
    WIDGET_ACTIVITIES,
    WIDGET_REPO_STATUS,
    WIDGET_FORTUNE,
} widget_kind_t;

/* Sprint 006 / T003: canonical cell footprints for rows-2-3 widgets.
 * All current widgets occupy 2 cells (UNIT_W_N(2) × UNIT_H). */
#define WIDGET_CELLS_GRAPH       2
#define WIDGET_CELLS_ACTIVITIES  2
#define WIDGET_CELLS_REPO_STATUS 2
#define WIDGET_CELLS_FORTUNE     2

typedef struct {
    widget_kind_t kind;
    uint8_t       cells;     /* size in grid cells (currently all = 2) */
    const void   *payload;   /* opaque, used by renderer (e.g. graph_host_series_t *) */
} widget_request_t;

#define LAYOUT_POOL_CAPACITY_CELLS 12  /* 6 cols × 2 rows */

/* Place widgets in the rows-2-3 pool per data-model.md §6:
 *  1. Stable-sort requests by kind ascending (enum order = priority).
 *  2. Greedy fill of a 12-cell budget; drop-and-continue when a request's
 *     cells would exceed remaining capacity.
 * Writes up to out_cap placed requests into out_placed and returns the
 * number written. Input array is not mutated. */
int layout_pool_place(const widget_request_t *requests, size_t n_requests,
                      widget_request_t *out_placed, size_t out_cap);

#endif /* REGISTRY_H */
