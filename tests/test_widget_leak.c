/* Spec 005 — widget leak regression suite.
 *
 * Phase 3 (T008/T018): test_memstat_sample_shape verifies the LVGL heap
 * invariant after a memstat sample and that uptime_s is monotonic across
 * consecutive samples.
 *
 * Phase 4 (T020-T024b) will add per-widget churn tests using
 * LEAK_TEST_MAX_GROWTH_BYTES as the tolerance constant.
 *
 * WI #4524: test_host_columns_churn — create, update and redraw the host
 * columns widget through every layout from 0 to 30 hosts, then destroy it.
 * LVGL here allocates from the C library (LV_STDLIB_CLIB), so the heap is
 * measured with mallinfo2(), not lv_mem_monitor().
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <malloc.h>

#include "hostcols.h"
#include "hosthist.h"
#include "lvgl.h"
#include "src/memstat.h"
#include "widgets/host_columns.h"

#define LEAK_TEST_MAX_GROWTH_BYTES (8u * 1024u)

static int g_failures = 0;

#define CHECK(cond, msg)                                                        \
    do {                                                                        \
        if (!(cond)) {                                                          \
            fprintf(stderr, "  FAIL: %s (%s:%d)\n", (msg), __FILE__, __LINE__); \
            g_failures++;                                                       \
        }                                                                       \
    } while (0)

/* Stubs: memstat_sample_now -> redis_write_mem_sample / status_push.
 * The test does not link the real redis.c or status.c, so we provide
 * no-op replacements here. */
void redis_write_mem_sample(const mem_sample_t *s) {
    (void)s;
}

typedef enum { STATUS_WARNING, STATUS_ERROR } status_severity_t;
void status_push(status_severity_t severity, const char *message) {
    (void)severity;
    (void)message;
}

static void test_memstat_sample_shape(void) {
    printf("test_memstat_sample_shape ...\n");

    memstat_init();

    mem_sample_t s1;
    memstat_take_sample(&s1);

    /* LVGL heap invariant: used + free == total. */
    CHECK((uint64_t)s1.lvgl_used + (uint64_t)s1.lvgl_free == (uint64_t)s1.lvgl_total,
          "lvgl_used + lvgl_free == lvgl_total");

    /* /proc/self/statm should always report some RSS. */
    CHECK(s1.rss_bytes > 0, "rss_bytes > 0");

    /* Log-line formatter produces non-empty, parseable output. */
    char line[256];
    int n = memstat_format_log_line(&s1, line, sizeof(line));
    CHECK(n > 0, "format_log_line returns >0");
    CHECK(strncmp(line, "memstat: ", 9) == 0, "log line has memstat: prefix");

    /* uptime_s must be monotonic across consecutive samples (closes
     * "Long uptime overflow" edge case in spec). */
    sleep(1);
    mem_sample_t s2;
    memstat_take_sample(&s2);
    CHECK(s2.uptime_s >= s1.uptime_s, "uptime_s monotonic");
}

static size_t heap_in_use(void) {
    return mallinfo2().uordblks;
}

static void flush_cb(lv_display_t *d, const lv_area_t *a, uint8_t *px) {
    (void)a;
    (void)px;
    lv_display_flush_ready(d);
}

/* One round: n hosts with values that move every call, so every slot is
 * repainted, then a real render pass so the draw path is exercised too. */
static void host_columns_round(lv_obj_t *w, hc_host_t *m, int n, int iter) {
    for (int i = 0; i < n; i++) {
        memset(&m[i], 0, sizeof(m[i]));
        snprintf(m[i].host, sizeof(m[i].host), "host%02d", i);
        snprintf(m[i].os, sizeof(m[i].os), "Debian 13");
        snprintf(m[i].status, sizeof(m[i].status), "up %dd", iter % 30);
        m[i].cpu = (iter + i) % 100;
        m[i].core = (iter * 3 + i) % 100;
        m[i].ram_pct = (iter + 7 * i) % 100;
        m[i].ram_att = hostcols_ram_att(m[i].ram_pct);
        m[i].att = m[i].ram_att;
        m[i].nodata = (i == 3 && iter % 2);
        m[i].gpu = (i % 3 == 0);
        m[i].gpu_pct = (iter * 5) % 100;
        snprintf(m[i].vram, sizeof(m[i].vram), "%d.0 / 24.0", iter % 24);
        m[i].ndisks = 1 + (iter + i) % HOSTCOLS_DISKS;
        for (int k = 0; k < m[i].ndisks; k++) {
            snprintf(m[i].disks[k].name, LABEL_LEN, "disk%d", k);
            m[i].disks[k].pct = (iter + k * 13) % 100;
        }
        m[i].worst = 0;
        hosthist_record(m[i].host, 1700000000.0 + iter * HOSTHIST_PERIOD_S, (float)m[i].cpu,
                        (float)m[i].core, (float)m[i].gpu_pct);
    }
    host_columns_update(w, m, n);
    lv_refr_now(NULL);
}

static void test_host_columns_churn(void) {
    printf("test_host_columns_churn ...\n");
    static hc_host_t m[HOSTCOLS_MAX];
    hosthist_reset();

    size_t before_create = heap_in_use();
    lv_obj_t *w = host_columns_create(lv_screen_active(), 0, 0);
    CHECK(w != NULL, "host_columns_create");

    /* Warm-up: one pass over every layout, so LVGL's own caches (glyphs,
     * draw buffers) are populated before the baseline is taken. */
    for (int n = 0; n <= HOSTCOLS_MAX; n++)
        host_columns_round(w, m, n, n);
    size_t baseline = heap_in_use();

    for (int iter = 0; iter < 400; iter++)
        host_columns_round(w, m, (iter * 7) % (HOSTCOLS_MAX + 1), iter);
    size_t after = heap_in_use();
    if (after > baseline + LEAK_TEST_MAX_GROWTH_BYTES)
        fprintf(stderr, "  host_columns churn grew the heap by %zu bytes\n", after - baseline);
    CHECK(after <= baseline + LEAK_TEST_MAX_GROWTH_BYTES, "host_columns churn heap growth");

    lv_obj_delete(w);
    lv_refr_now(NULL);
    size_t after_delete = heap_in_use();
    CHECK(after_delete <= before_create + LEAK_TEST_MAX_GROWTH_BYTES * 8,
          "host_columns destroy returns its memory");
}

int main(void) {
    lv_init();
    lv_display_t *disp = lv_display_create(1896, 628);
    static uint8_t buf[1896 * 64 * 4];
    lv_display_set_buffers(disp, buf, NULL, sizeof(buf), LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_flush_cb(disp, flush_cb);

    test_memstat_sample_shape();
    test_host_columns_churn();

    if (g_failures == 0) {
        printf("test_widget_leak: PASS (2 cases)\n");
        return 0;
    }
    printf("test_widget_leak: FAIL (%d failures)\n", g_failures);
    return 1;
}
