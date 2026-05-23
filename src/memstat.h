#ifndef KPIDASH_MEMSTAT_H
#define KPIDASH_MEMSTAT_H

#include <stdint.h>
#include <time.h>

/* Spec 005: in-process memory telemetry.
 *
 * memstat samples process RSS/VSize from /proc/self/statm and LVGL heap
 * counters via lv_mem_monitor(), emits a single-line log record, and
 * publishes the latest sample + a ring of recent samples to Redis.
 *
 * The header keeps a fixed, parseable schema so external tools can diff
 * baseline vs. final samples to verify SC-001/SC-002.
 */

typedef struct mem_sample_s {
    time_t t;                   /* wall-clock unix seconds */
    uint64_t uptime_s;          /* monotonic seconds since memstat_init() */
    uint64_t rss_bytes;         /* /proc/self/statm field 2 * pagesize */
    uint64_t vsize_bytes;       /* /proc/self/statm field 1 * pagesize */
    uint32_t lvgl_total;        /* lv_mem_monitor_t.total_size */
    uint32_t lvgl_free;         /* lv_mem_monitor_t.free_size */
    uint32_t lvgl_used;         /* total - free */
    uint32_t lvgl_max_used;     /* lv_mem_monitor_t.max_used */
    uint8_t lvgl_frag_pct;      /* lv_mem_monitor_t.frag_pct */
    uint32_t lvgl_free_biggest; /* lv_mem_monitor_t.free_biggest_size */
} mem_sample_t;

/* High-water threshold and warning rate-limit (compile-time per FR-010). */
#ifndef KPIDASH_MEM_HIGH_WATER_BYTES
#define KPIDASH_MEM_HIGH_WATER_BYTES (8u * 1024u * 1024u) /* 8 MiB */
#endif
#ifndef KPIDASH_MEM_WARN_COOLDOWN_S
#define KPIDASH_MEM_WARN_COOLDOWN_S 300 /* 5 minutes */
#endif

/* Initialise the module: records start time used for uptime_s. */
void memstat_init(void);

/* Take one sample, log it, and publish to Redis. */
void memstat_sample_now(void);

/* Populate *out with a fresh sample (no log emission, no Redis write).
 * Exposed so the regression test can inspect field values directly. */
void memstat_take_sample(mem_sample_t *out);

/* Render the canonical one-line log record for *s into buf.
 * Returns the number of bytes written (excluding NUL), or 0 on bad args. */
int memstat_format_log_line(const mem_sample_t *s, char *buf, size_t buflen);

#endif /* KPIDASH_MEMSTAT_H */
