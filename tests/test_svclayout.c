/* test_svclayout.c — the service card's geometry and line fitting
 * (WI #4847, sprint 048). Pure: no LVGL, no Redis. */
#include "svclayout.h"

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

/* A span-2 card covers exactly two span-1 cards and the gap between them. */
static void test_width(void) {
    CHECK(svc_card_width(1) == 220);
    CHECK(svc_card_width(2) == 2 * 220 + 8);
    CHECK(svc_card_width(2) == 2 * svc_card_width(1) + SVC_STRIP_GAP);
    /* Zero is a fresh registry slot; anything else is malformed: one unit. */
    CHECK(svc_card_width(0) == 220);
    CHECK(svc_card_width(3) == 220);
    CHECK(svc_card_width(-1) == 220);
}

static void test_fit_all(void) {
    const int32_t h[] = {19, 24, 24, 24};
    CHECK(svc_lines_fit(h, 4, 200, 2) == 4);
    /* Exactly enough: 19 + 3 * (2 + 24) = 97. */
    CHECK(svc_lines_fit(h, 4, 97, 2) == 4);
}

/* One pixel short of the last line drops that line, whole. */
static void test_fit_clips_from_bottom(void) {
    const int32_t h[] = {19, 24, 24, 24};
    CHECK(svc_lines_fit(h, 4, 96, 2) == 3);
    CHECK(svc_lines_fit(h, 4, 19 + 2 + 24, 2) == 2);
    CHECK(svc_lines_fit(h, 4, 19, 2) == 1);
    CHECK(svc_lines_fit(h, 4, 18, 2) == 0);
}

/* The first line pays no gap; each later one does. */
static void test_fit_gap_between_only(void) {
    const int32_t h[] = {20, 20};
    CHECK(svc_lines_fit(h, 2, 40, 0) == 2);
    CHECK(svc_lines_fit(h, 2, 40, 1) == 1);
    CHECK(svc_lines_fit(h, 2, 41, 1) == 2);
}

/* A tall line that does not fit stops the fill: later, shorter lines are not
 * pulled up past it, because they would then read as its neighbour's. */
static void test_fit_stops_at_first_miss(void) {
    const int32_t h[] = {19, 33, 19};
    CHECK(svc_lines_fit(h, 3, 19 + 19 + 4, 2) == 1);
}

static void test_fit_degenerate(void) {
    const int32_t h[] = {19};
    CHECK(svc_lines_fit(NULL, 1, 100, 0) == 0);
    CHECK(svc_lines_fit(h, 0, 100, 0) == 0);
    CHECK(svc_lines_fit(h, 1, 0, 0) == 0);
    CHECK(svc_lines_fit(h, 1, -5, 0) == 0);
}

/* Eight S lines under the full header do not all fit a 240-px card; this is
 * the arithmetic, with the widget's real numbers, that says so. Content is
 * 240 - 2 * (6 + 8) = 212; icon, name and host plus their gaps leave the
 * rest. The render tool checks the same thing against LVGL's own layout. */
static void test_fit_eight_small_under_header(void) {
    int32_t h[KDASH_SVC_LINES_MAX];
    for (int i = 0; i < KDASH_SVC_LINES_MAX; i++) h[i] = 19;
    CHECK(svc_lines_fit(h, KDASH_SVC_LINES_MAX, 212, 0) == KDASH_SVC_LINES_MAX);
    CHECK(svc_lines_fit(h, KDASH_SVC_LINES_MAX, 100, 0) == 5);
}

static void test_sig(void) {
    kdash_service_line_t a[2];
    memset(a, 0, sizeof(a));
    snprintf(a[0].t, sizeof(a[0].t), "one");
    snprintf(a[1].t, sizeof(a[1].t), "two");
    uint32_t base = svc_layout_sig("t", 1, "x", "kai", 3, a, 2);
    CHECK(base == svc_layout_sig("t", 1, "x", "kai", 3, a, 2));

    CHECK(base != svc_layout_sig("u", 1, "x", "kai", 3, a, 2));
    CHECK(base != svc_layout_sig("t", 2, "x", "kai", 3, a, 2));
    CHECK(base != svc_layout_sig("t", 1, "y", "kai", 3, a, 2));
    CHECK(base != svc_layout_sig("t", 1, "x", "_", 3, a, 2));
    CHECK(base != svc_layout_sig("t", 1, "x", "kai", 4, a, 2));
    CHECK(base != svc_layout_sig("t", 1, "x", "kai", 3, a, 1));

    kdash_service_line_t b[2];
    memcpy(b, a, sizeof(b));
    b[1].align = KDASH_ALIGN_R;
    CHECK(base != svc_layout_sig("t", 1, "x", "kai", 3, b, 2));
    memcpy(b, a, sizeof(b));
    b[0].size = KDASH_LINE_S;
    CHECK(base != svc_layout_sig("t", 1, "x", "kai", 3, b, 2));
    memcpy(b, a, sizeof(b));
    b[0].has_color = true;
    CHECK(base != svc_layout_sig("t", 1, "x", "kai", 3, b, 2));
    b[0].color = 0xD20F39;
    uint32_t red = svc_layout_sig("t", 1, "x", "kai", 3, b, 2);
    b[0].color = 0x1E66F5;
    CHECK(red != svc_layout_sig("t", 1, "x", "kai", 3, b, 2));

    /* Field boundaries count: "ab"+"c" is not "a"+"bc". */
    CHECK(svc_layout_sig("ab", 1, "c", "", 0, NULL, 0) !=
          svc_layout_sig("a", 1, "bc", "", 0, NULL, 0));
}

int main(void) {
    test_width();
    test_fit_all();
    test_fit_clips_from_bottom();
    test_fit_gap_between_only();
    test_fit_stops_at_first_miss();
    test_fit_degenerate();
    test_fit_eight_small_under_header();
    test_sig();
    fprintf(stderr, "test_svclayout: %d passed, %d failed\n", passed, failed);
    return failed == 0 ? 0 : 1;
}
