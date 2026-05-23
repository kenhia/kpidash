/* Spec 005 — widget leak regression suite.
 *
 * Phase 3 (T008/T018): test_memstat_sample_shape verifies the LVGL heap
 * invariant after a memstat sample and that uptime_s is monotonic across
 * consecutive samples.
 *
 * Phase 4 (T020-T024b) will add per-widget churn tests using
 * LEAK_TEST_MAX_GROWTH_BYTES as the tolerance constant.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "lvgl.h"
#include "src/memstat.h"

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

int main(void) {
    lv_init();

    test_memstat_sample_shape();

    if (g_failures == 0) {
        printf("test_widget_leak: PASS (1 case)\n");
        return 0;
    }
    printf("test_widget_leak: FAIL (%d failures)\n", g_failures);
    return 1;
}
