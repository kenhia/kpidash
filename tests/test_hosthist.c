/* test_hosthist.c — the per-host history ring behind the host columns
 * sparklines (WI #4523): slotting, wrap, and the two gap rules — a missed poll
 * is bridged, an absence is not, and neither ever reads as a drop to zero. */
#include "hosthist.h"

#include <stdio.h>
#include <string.h>

static int passed = 0;
static int failed = 0;

#define CHECK(expr)                                                                                \
    do {                                                                                           \
        if (!(expr)) {                                                                             \
            fprintf(stderr, "FAIL [%s:%d]: %s\n", __FILE__, __LINE__, #expr);                      \
            failed++;                                                                              \
        } else {                                                                                   \
            passed++;                                                                              \
        }                                                                                          \
    } while (0)

#define P HOSTHIST_PERIOD_S
/* A slot-aligned epoch, so slot arithmetic in the tests is exact. */
#define T0 (1700000000.0 - ((long)1700000000 % P))

static float cpu[HOSTHIST_LEN], core[HOSTHIST_LEN], gpu[HOSTHIST_LEN];

static void test_empty(void) {
    hosthist_reset();
    CHECK(hosthist_read("kai", cpu, core, gpu, HOSTHIST_LEN) == 0);
    CHECK(hosthist_read(NULL, cpu, core, gpu, HOSTHIST_LEN) == 0);
    hosthist_record(NULL, T0, 1, 2, 3); /* ignored, not a crash */
    hosthist_record("", T0, 1, 2, 3);
    CHECK(hosthist_read("", cpu, core, gpu, HOSTHIST_LEN) == 0);
}

static void test_order_and_fields(void) {
    hosthist_reset();
    for (int i = 0; i < 5; i++)
        hosthist_record("kai", T0 + i * P, (float)i, (float)(10 + i), (float)(20 + i));
    int n = hosthist_read("kai", cpu, core, gpu, HOSTHIST_LEN);
    CHECK(n == 5);
    CHECK(cpu[0] == 0.0f && cpu[4] == 4.0f);
    CHECK(core[0] == 10.0f && core[4] == 14.0f);
    CHECK(gpu[0] == 20.0f && gpu[4] == 24.0f);
    /* NULL arrays are allowed; max truncates to the NEWEST samples. */
    CHECK(hosthist_read("kai", NULL, NULL, NULL, HOSTHIST_LEN) == 5);
    CHECK(hosthist_read("kai", cpu, NULL, NULL, 2) == 2);
    CHECK(cpu[0] == 3.0f && cpu[1] == 4.0f);
}

static void test_same_slot_overwrites(void) {
    hosthist_reset();
    hosthist_record("kai", T0, 5, 5, 5);
    hosthist_record("kai", T0 + 1, 7, 8, 9); /* same 4 s slot: latest wins */
    CHECK(hosthist_read("kai", cpu, core, gpu, HOSTHIST_LEN) == 1);
    CHECK(cpu[0] == 7.0f && core[0] == 8.0f && gpu[0] == 9.0f);
    /* A clock that steps backwards overwrites rather than inserting. */
    hosthist_record("kai", T0 - 100, 1, 1, 1);
    CHECK(hosthist_read("kai", cpu, NULL, NULL, HOSTHIST_LEN) == 1);
    CHECK(cpu[0] == 1.0f);
}

static void test_wrap(void) {
    hosthist_reset();
    int total = HOSTHIST_LEN + 37;
    for (int i = 0; i < total; i++)
        hosthist_record("kai", T0 + i * P, (float)i, 0, 0);
    int n = hosthist_read("kai", cpu, NULL, NULL, HOSTHIST_LEN);
    CHECK(n == HOSTHIST_LEN);
    CHECK(cpu[0] == (float)(total - HOSTHIST_LEN));
    CHECK(cpu[HOSTHIST_LEN - 1] == (float)(total - 1));
    for (int i = 1; i < n; i++)
        if (cpu[i] != cpu[i - 1] + 1.0f) {
            CHECK(!"ring out of order after wrap");
            break;
        }
}

static void test_short_gap_is_held(void) {
    hosthist_reset();
    hosthist_record("kai", T0, 40, 50, 60);
    /* Two slots missed, then a reading: the gap holds 40, never 0. */
    hosthist_record("kai", T0 + 3 * P, 10, 20, 30);
    int n = hosthist_read("kai", cpu, core, gpu, HOSTHIST_LEN);
    CHECK(n == 4);
    CHECK(cpu[1] == 40.0f && cpu[2] == 40.0f && cpu[3] == 10.0f);
    CHECK(core[1] == 50.0f && gpu[2] == 60.0f);
}

static void test_long_gap_not_filled(void) {
    hosthist_reset();
    hosthist_record("kai", T0, 40, 0, 0);
    hosthist_record("kai", T0 + P, 41, 0, 0);
    /* Away for an hour: no flat line invented across the absence. */
    hosthist_record("kai", T0 + 3600, 12, 0, 0);
    int n = hosthist_read("kai", cpu, NULL, NULL, HOSTHIST_LEN);
    CHECK(n == 3);
    CHECK(cpu[0] == 40.0f && cpu[1] == 41.0f && cpu[2] == 12.0f);
    /* The gap boundary itself: HOSTHIST_GAP_FILL missing slots are filled,
     * one more is not. */
    hosthist_reset();
    hosthist_record("a", T0, 1, 0, 0);
    hosthist_record("a", T0 + (HOSTHIST_GAP_FILL + 1) * P, 2, 0, 0);
    CHECK(hosthist_read("a", NULL, NULL, NULL, HOSTHIST_LEN) == HOSTHIST_GAP_FILL + 2);
    hosthist_record("b", T0, 1, 0, 0);
    hosthist_record("b", T0 + (HOSTHIST_GAP_FILL + 2) * P, 2, 0, 0);
    CHECK(hosthist_read("b", NULL, NULL, NULL, HOSTHIST_LEN) == 2);
}

static void test_stopped_host_keeps_trace(void) {
    hosthist_reset();
    for (int i = 0; i < 10; i++)
        hosthist_record("thrall", T0 + i * P, 30, 0, 0);
    /* Nothing more is recorded for thrall while other hosts carry on. */
    for (int i = 10; i < 400; i++)
        hosthist_record("kai", T0 + i * P, 1, 0, 0);
    int n = hosthist_read("thrall", cpu, NULL, NULL, HOSTHIST_LEN);
    CHECK(n == 10);
    CHECK(cpu[9] == 30.0f);
}

static void test_table_full_evicts_least_recent(void) {
    hosthist_reset();
    char name[16];
    for (int i = 0; i < HOSTHIST_MAX_HOSTS; i++) {
        snprintf(name, sizeof(name), "h%02d", i);
        hosthist_record(name, T0 + i, (float)i, 0, 0);
    }
    /* h00 is refreshed, so h01 is now the least recently recorded. */
    hosthist_record("h00", T0 + 100, 99, 0, 0);
    hosthist_record("newcomer", T0 + 200, 7, 0, 0);
    CHECK(hosthist_read("newcomer", cpu, NULL, NULL, HOSTHIST_LEN) == 1);
    CHECK(cpu[0] == 7.0f);
    CHECK(hosthist_read("h01", NULL, NULL, NULL, HOSTHIST_LEN) == 0);
    CHECK(hosthist_read("h00", NULL, NULL, NULL, HOSTHIST_LEN) >= 1);
    CHECK(hosthist_read("h02", NULL, NULL, NULL, HOSTHIST_LEN) == 1);
}

int main(void) {
    test_empty();
    test_order_and_fields();
    test_same_slot_overwrites();
    test_wrap();
    test_short_gap_is_held();
    test_long_gap_not_filled();
    test_stopped_host_keeps_trace();
    test_table_full_evicts_least_recent();
    printf("test_hosthist: %d passed, %d failed\n", passed, failed);
    return failed ? 1 : 0;
}
