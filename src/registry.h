#ifndef REGISTRY_H
#define REGISTRY_H

#include <stdbool.h>
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
    bool graph_enabled;
    char graph_client[HOSTNAME_LEN]; /* hostname of client to graph */
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
 * Returns NULL if the registry is full.
 */
client_info_t *registry_find_or_create(const char *hostname);

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

/* ---- Graph host series (T006) ---- */
#define GRAPH_HOST_MAX 8
#define GRAPH_HOST_STALE_SECONDS 30.0

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
