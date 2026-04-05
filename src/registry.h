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
    int grid_size; /* pixels; 0 = disabled */
    bool textsize_enabled;
    bool graph_enabled;
    char graph_client[HOSTNAME_LEN]; /* hostname of client to graph */
} dev_cmd_state_t;

/* ---- Dev telemetry snapshot (fast GPU+CPU+RAM from dev_telemetry key) ---- */
typedef struct {
    bool valid; /* true if key was present and parsed */
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

#endif /* REGISTRY_H */
