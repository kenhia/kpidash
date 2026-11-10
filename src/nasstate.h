#ifndef NASSTATE_H
#define NASSTATE_H

/**
 * nasstate.h — the NAS big panel's model (WI #3956).
 *
 * Reads kdash:nas:{host} v1 (kdashdata sprint 020, schema kdash-nas) for ONE
 * host, KPIDASH_NAS_HOST, and derives what the panel draws: per-pool and
 * per-disk attention levels and the panel's freshness. Pure — no LVGL, no
 * Redis, no clock — so all of it is reachable from the gate.
 *
 * Freshness is the host cards' convention (as for kdash:net): the producer
 * writes every ~5 s with EX 15, so a key ABSENT this poll means the publisher
 * is gone. The panel keeps the last record and reads STALE, never healthy. A
 * host that has never published is NO FEED, and the panel is not placed at
 * all — gratch keeps its ordinary dev graph.
 *
 * Two clocks, deliberately: `ts` is the producer's, `collected_ts` the
 * collector's. A live producer republishing a dead collector's data is
 * `source_stale`, and the panel says so.
 *
 * Touched only from the LVGL thread (the 1 s poll and ui_refresh both run in
 * main.c's timer callback), so there is no lock.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define NAS_POOLS_MAX 4
#define NAS_DISKS_MAX 12
/* EX 15 on the producer side; a record older than this is stale even if a
 * clock-skewed key is still present. */
#define NAS_FRESH_S 15.0

typedef enum { NAS_FEED_NONE = 0, NAS_FEED_LIVE, NAS_FEED_STALE } nas_feed_t;

/* Ordered by severity so a max() is the worst. NEUTRAL is "shown, not a
 * fault" — a standby disk, a non-zero reallocated count with no history. */
typedef enum { NAS_OK = 0, NAS_NEUTRAL, NAS_WARN, NAS_CRIT } nas_level_t;

typedef struct {
    char name[32];
    char health[12];
    int64_t used_bytes;
    int64_t avail_bytes;
    int64_t errors;       /* -1 = null */
    bool has_scrub;
    char scrub_state[12]; /* scanning | finished | canceled */
    double scrub_end_ts;  /* 0 while scanning or unknown */
    int64_t scrub_errors;
} nas_pool_t;

typedef struct {
    char device[24];
    char model[48];
    char kind[8];        /* hdd | ssd | nvme */
    char power_state[10];/* active | standby | unknown */
    int smart_passed;    /* -1 null, 0 failed, 1 passed */
    double smart_ts;     /* 0 = null */
    bool has_temp;
    double temp_c;
    int64_t reallocated_sectors; /* -1 = null, likewise below */
    int64_t pending_sectors;
    int64_t percentage_used;
    int64_t media_errors;
} nas_disk_t;

typedef struct {
    double ts;
    bool has_collected;
    double collected_ts;
    bool source_stale;
    nas_pool_t pools[NAS_POOLS_MAX];
    int npools;
    nas_disk_t disks[NAS_DISKS_MAX];
    int ndisks;
} nas_record_t;

/* Parse one payload. 0 on success; -1 when it is not a v1 record (missing
 * `ts`, not an object, unparseable) — the caller keeps the previous one. */
int nas_parse(const char *json, nas_record_t *out);

/* Feed one poll: `json` is the GET result, NULL when the key is absent. */
void nas_observe(const char *json, double now);

/* True once a record has been accepted this run. */
bool nas_has_record(void);
const nas_record_t *nas_record(void);
nas_feed_t nas_feed(double now);

nas_level_t nas_pool_level(const nas_pool_t *p);
nas_level_t nas_disk_level(const nas_disk_t *d);
/* Worst of every pool, every disk and the collector. */
nas_level_t nas_overall_level(const nas_record_t *r);

/* "45 s", "12 m", "3 h", "2 d". */
void nas_format_age(double seconds, char *buf, size_t n);
/* "921 GB", "5.3 TB". */
void nas_format_bytes(int64_t bytes, char *buf, size_t n);

/* Test hook: forget everything. */
void nas_reset(void);

#endif /* NASSTATE_H */
