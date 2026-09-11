/* test_apttemps_band.c — the apartment-temperature band boundaries (WI #2244).
 *
 * This is a CHARACTERISATION test, and the distinction matters. WI #2244
 * replaces kpidash's own threshold arithmetic in apttemps_color() with a call
 * to libkdash's kdash_apttemps_band(), and its acceptance is that NOTHING
 * VISIBLE CHANGES: the same three zones must render the same colours they did
 * before the repoint.
 *
 * "No visible change" is only a testable claim if the boundaries are pinned on
 * BOTH sides — 64.9 blue and 65.0 green, 75.0 green and 75.01 orange, 79.9
 * orange and 80.0 red. A test that checked only midpoints would pass happily
 * while an off-by-one in the comparison operators moved a band edge, which is
 * exactly the failure this repoint could introduce and the panel would show.
 *
 * These assertions were written and run GREEN against the pre-repoint
 * arithmetic, so their authority comes from the old implementation rather than
 * from the new one agreeing with itself.
 *
 * Staleness is tested too, because it outranks the number in both
 * implementations: an unrefreshed reading renders GRAY however comfortable it
 * looks.
 */
#define KPIDASH_TEST_STUBS 1
#include "registry.h"

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

/* A fresh, valid entry at `t` degrees F. `now` is fixed at 1000.0 across the
 * suite and the payload ts sits one second back, so every temperature case is
 * unambiguously inside the freshness window. */
static apttemps_entry_t at(float t) {
    apttemps_entry_t e;
    memset(&e, 0, sizeof(e));
    snprintf(e.slug, sizeof(e.slug), "bedroom");
    e.temp_f = t;
    e.valid = true;
    e.last_payload_ts = 999.0;
    return e;
}

#define NOW 1000.0

static void test_band_boundaries(void) {
    /* COLD/BLUE: everything below 65.0 */
    apttemps_entry_t e = at(40.0f);  CHECK(apttemps_color(&e, NOW) == APTTEMPS_COLOR_BLUE);
    e = at(64.0f);                   CHECK(apttemps_color(&e, NOW) == APTTEMPS_COLOR_BLUE);
    e = at(64.9f);                   CHECK(apttemps_color(&e, NOW) == APTTEMPS_COLOR_BLUE);

    /* 65.0 is the first GREEN — the cold edge, from below and on it. */
    e = at(65.0f);                   CHECK(apttemps_color(&e, NOW) == APTTEMPS_COLOR_GREEN);
    e = at(70.0f);                   CHECK(apttemps_color(&e, NOW) == APTTEMPS_COLOR_GREEN);

    /* 75.0 is still GREEN (inclusive), 75.01 is the first ORANGE. */
    e = at(75.0f);                   CHECK(apttemps_color(&e, NOW) == APTTEMPS_COLOR_GREEN);
    e = at(75.01f);                  CHECK(apttemps_color(&e, NOW) == APTTEMPS_COLOR_ORANGE);
    e = at(77.5f);                   CHECK(apttemps_color(&e, NOW) == APTTEMPS_COLOR_ORANGE);
    e = at(79.9f);                   CHECK(apttemps_color(&e, NOW) == APTTEMPS_COLOR_ORANGE);

    /* 80.0 is the first RED — the hot edge, from below and on it. */
    e = at(80.0f);                   CHECK(apttemps_color(&e, NOW) == APTTEMPS_COLOR_RED);
    e = at(95.0f);                   CHECK(apttemps_color(&e, NOW) == APTTEMPS_COLOR_RED);
}

static void test_staleness_beats_the_number(void) {
    /* A temperature that would be GREEN renders GRAY once the payload ages
     * past the window. The window is exclusive at its own edge: at exactly
     * APTTEMPS_FRESH_SECONDS the entry is already stale. */
    apttemps_entry_t e = at(70.0f);

    e.last_payload_ts = NOW - (APTTEMPS_FRESH_SECONDS - 0.1);
    CHECK(apttemps_color(&e, NOW) == APTTEMPS_COLOR_GREEN);

    e.last_payload_ts = NOW - APTTEMPS_FRESH_SECONDS;
    CHECK(apttemps_color(&e, NOW) == APTTEMPS_COLOR_GRAY);

    e.last_payload_ts = NOW - (APTTEMPS_FRESH_SECONDS * 10.0);
    CHECK(apttemps_color(&e, NOW) == APTTEMPS_COLOR_GRAY);

    /* Staleness outranks a RED temperature too — the card must not claim the
     * room is hot on evidence it no longer has. */
    e = at(95.0f);
    e.last_payload_ts = NOW - (APTTEMPS_FRESH_SECONDS * 2.0);
    CHECK(apttemps_color(&e, NOW) == APTTEMPS_COLOR_GRAY);
}

static void test_invalid_and_null(void) {
    /* An entry that never took a payload is GRAY regardless of the zeroed
     * temperature field, which would otherwise read as a very cold room. */
    apttemps_entry_t e = at(70.0f);
    e.valid = false;
    CHECK(apttemps_color(&e, NOW) == APTTEMPS_COLOR_GRAY);

    CHECK(apttemps_color(NULL, NOW) == APTTEMPS_COLOR_GRAY);
}

int main(void) {
    test_band_boundaries();
    test_staleness_beats_the_number();
    test_invalid_and_null();

    printf("test_apttemps_band: %d passed, %d failed\n", passed, failed);
    return failed == 0 ? 0 : 1;
}
