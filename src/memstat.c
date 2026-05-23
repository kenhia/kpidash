#include "memstat.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "lvgl.h"
#include "redis.h"
#include "status.h"

/* Module-private state: monotonic anchor + warning rate-limit clock. */
static struct timespec g_start_mono;
static time_t g_last_warn_wall; /* wall-clock seconds; 0 = never */

/* Read /proc/self/statm. Fields (in pages):
 *   1: size (total program size, ~ VSize)
 *   2: resident (RSS)
 * Multiply by sysconf(_SC_PAGESIZE) for bytes. Returns true on success.
 */
static bool read_proc_self_statm(uint64_t *out_vsize_bytes, uint64_t *out_rss_bytes) {
    FILE *f = fopen("/proc/self/statm", "r");
    if (!f)
        return false;
    unsigned long size_pages = 0, rss_pages = 0;
    int n = fscanf(f, "%lu %lu", &size_pages, &rss_pages);
    fclose(f);
    if (n < 2)
        return false;
    long page = sysconf(_SC_PAGESIZE);
    if (page <= 0)
        page = 4096;
    *out_vsize_bytes = (uint64_t)size_pages * (uint64_t)page;
    *out_rss_bytes = (uint64_t)rss_pages * (uint64_t)page;
    return true;
}

void memstat_init(void) {
    clock_gettime(CLOCK_MONOTONIC, &g_start_mono);
    g_last_warn_wall = 0;
}

void memstat_take_sample(mem_sample_t *out) {
    if (!out)
        return;
    memset(out, 0, sizeof(*out));

    out->t = time(NULL);

    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    if (g_start_mono.tv_sec == 0 && g_start_mono.tv_nsec == 0)
        g_start_mono = now; /* defensive: caller forgot memstat_init() */
    int64_t delta = (int64_t)(now.tv_sec - g_start_mono.tv_sec);
    if (delta < 0)
        delta = 0;
    out->uptime_s = (uint64_t)delta;

    read_proc_self_statm(&out->vsize_bytes, &out->rss_bytes);

    /* lv_mem_monitor() is always available in lib/lvgl/src/stdlib/lv_mem.c;
     * the LV_USE_MEM_MONITOR flag only governs the on-screen overlay. */
    lv_mem_monitor_t mon;
    lv_mem_monitor(&mon);
    out->lvgl_total = (uint32_t)mon.total_size;
    out->lvgl_free = (uint32_t)mon.free_size;
    out->lvgl_used = (uint32_t)(mon.total_size - mon.free_size);
    out->lvgl_max_used = (uint32_t)mon.max_used;
    out->lvgl_frag_pct = mon.frag_pct;
    out->lvgl_free_biggest = (uint32_t)mon.free_biggest_size;
}

int memstat_format_log_line(const mem_sample_t *s, char *buf, size_t buflen) {
    if (!s || !buf || buflen == 0)
        return 0;
    /* Exact field order from data-model.md. */
    int n = snprintf(buf, buflen,
                     "memstat: t=%lld up=%llu rss=%llu vsize=%llu lvgl_used=%u lvgl_max=%u "
                     "lvgl_total=%u lvgl_frag=%u lvgl_free_biggest=%u",
                     (long long)s->t, (unsigned long long)s->uptime_s,
                     (unsigned long long)s->rss_bytes, (unsigned long long)s->vsize_bytes,
                     (unsigned)s->lvgl_used, (unsigned)s->lvgl_max_used, (unsigned)s->lvgl_total,
                     (unsigned)s->lvgl_frag_pct, (unsigned)s->lvgl_free_biggest);
    if (n < 0)
        return 0;
    return n;
}

void memstat_sample_now(void) {
    mem_sample_t s;
    memstat_take_sample(&s);

    char line[256];
    int n = memstat_format_log_line(&s, line, sizeof(line));
    if (n > 0)
        fprintf(stderr, "%s\n", line);

    /* T017: high-water warning, rate-limited (FR-010). */
    if (s.lvgl_used > KPIDASH_MEM_HIGH_WATER_BYTES) {
        if (g_last_warn_wall == 0 ||
            (s.t - g_last_warn_wall) >= (time_t)KPIDASH_MEM_WARN_COOLDOWN_S) {
            fprintf(stderr, "memstat: WARN lvgl_used=%u over high-water=%u\n",
                    (unsigned)s.lvgl_used, (unsigned)KPIDASH_MEM_HIGH_WATER_BYTES);
            status_push(STATUS_WARNING, "LVGL heap high-water exceeded");
            g_last_warn_wall = s.t;
        }
    }

    redis_write_mem_sample(&s);
}
