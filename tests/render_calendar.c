/* render_calendar.c — offline render of the time and calendar card to BMP
 * (sprint 040). Not a test: a review tool, built with the full native tree (it
 * needs LVGL), so the card can be compared with the mock-up without a Pi.
 *
 *   render_calendar <out.bmp> [now|d27|six|nofeed|stale|outdated]
 *   render_calendar <out.bmp> live <calendar.json> [unix-time]
 *
 * now, d27 and six are the mock-up's three states (calendar-mockups.html,
 * `S.now` / `S.d27` / `S.six`), with the 24-hour clock Ken asked for:
 *   now      Friday 2026-11-13 18:42, November's tags
 *   d27      Friday 2026-11-27 09:15: today is a day off and a birthday
 *   six      Friday 2026-08-14 18:42, a six-week month, made-up tags
 * and three it does not draw:
 *   nofeed   the key has never published: grid and weekends only
 *   stale    November's record read 3 hours after its ts: the muted hint
 *   outdated November's record on 2027-02-20, past its window: untagged
 * live reads a record from a file and draws it at the given time (default:
 * the clock). The zone is pinned to America/Los_Angeles, rpi53's. The canvas
 * is the card's whole 1264 × 628 frame, the card at CELL_PAD inside it, on
 * the panel's crust. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "bmp_write.h"
#include "calstate.h"
#include "layout.h"
#include "lvgl.h"
#include "widgets/calendar_card.h"

static void flush_cb(lv_display_t *d, const lv_area_t *a, uint8_t *px) {
    (void)a;
    (void)px;
    lv_display_flush_ready(d);
}

static char *slurp(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *buf = malloc((size_t)n + 1);
    if (buf && fread(buf, 1, (size_t)n, f) != (size_t)n) {
        free(buf);
        buf = NULL;
    }
    if (buf) buf[n] = '\0';
    fclose(f);
    return buf;
}

static time_t local(int y, int m, int d, int hh, int mm) {
    struct tm tm = {0};
    tm.tm_year = y - 1900;
    tm.tm_mon = m - 1;
    tm.tm_mday = d;
    tm.tm_hour = hh;
    tm.tm_min = mm;
    tm.tm_isdst = -1;
    return mktime(&tm);
}

/* The mock-up's NOV, as a contract record: the 25th and 27th off, the 26th
 * Thanksgiving (a holiday, not taken off), the 27th also a birthday,
 * Veterans Day worked. Then the
 * window's later holidays, so the list can run past the month. */
static const char *NOV =
    "{\"ts\":%.1f,\"expected_refresh_s\":3600,\"from\":\"2026-11-01\","
    "\"to\":\"2027-02-14\",\"days\":["
    "{\"date\":\"2026-11-11\",\"off\":false,\"holiday\":\"Veterans Day\",\"short\":\"Veterans\"},"
    "{\"date\":\"2026-11-25\",\"off\":true},"
    "{\"date\":\"2026-11-26\",\"off\":false,\"holiday\":\"Thanksgiving\",\"short\":\"Thanksgiving\"},"
    "{\"date\":\"2026-11-27\",\"off\":true,\"special\":\"Lynn's birthday\",\"short\":\"Lynn\"},"
    "{\"date\":\"2026-12-25\",\"off\":true,\"holiday\":\"Christmas Day\",\"short\":\"Christmas\"},"
    "{\"date\":\"2027-01-01\",\"off\":true,\"holiday\":\"New Year's Day\",\"short\":\"New Year\"},"
    "{\"date\":\"2027-01-18\",\"off\":false,\"holiday\":\"Martin Luther King Jr. Day\","
    "\"short\":\"MLK Day\"}]}";

/* The mock-up's AUG (made up for the picture). */
static const char *AUG =
    "{\"ts\":%.1f,\"expected_refresh_s\":3600,\"from\":\"2026-08-01\","
    "\"to\":\"2026-11-14\",\"days\":["
    "{\"date\":\"2026-08-20\",\"off\":true},"
    "{\"date\":\"2026-08-21\",\"off\":true},"
    "{\"date\":\"2026-08-28\",\"off\":false,\"special\":\"Example day\",\"short\":\"Example\"},"
    "{\"date\":\"2026-09-07\",\"off\":true,\"holiday\":\"Labor Day\",\"short\":\"Labor Day\"}]}";

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr,
                "usage: %s out.bmp [now|d27|six|nofeed|stale|outdated]\n"
                "       %s out.bmp live calendar.json [unix-time]\n",
                argv[0], argv[0]);
        return 2;
    }
    const char *state = argc > 2 ? argv[2] : "now";
    setenv("TZ", "America/Los_Angeles", 1);
    tzset();
    cal_reset();

    static char rec[4096];
    time_t now;
    if (strcmp(state, "live") == 0) {
        if (argc < 4) {
            fprintf(stderr, "live needs calendar.json\n");
            return 2;
        }
        char *s = slurp(argv[3]);
        if (!s || cal_observe(s) != 1) {
            fprintf(stderr, "cannot read the record\n");
            return 1;
        }
        free(s);
        now = argc > 4 ? (time_t)atoll(argv[4]) : time(NULL);
    } else if (strcmp(state, "six") == 0) {
        now = local(2026, 8, 14, 18, 42);
        snprintf(rec, sizeof(rec), AUG, (double)now - 600);
        cal_observe(rec);
    } else if (strcmp(state, "nofeed") == 0) {
        now = local(2026, 11, 13, 18, 42);
    } else {
        if (strcmp(state, "d27") == 0)
            now = local(2026, 11, 27, 9, 15);
        else if (strcmp(state, "outdated") == 0)
            now = local(2027, 2, 20, 18, 42);
        else
            now = local(2026, 11, 13, 18, 42);
        double ts = (double)now - 600;
        if (strcmp(state, "stale") == 0)
            ts = (double)now - 3 * 3600 - 300;
        else if (strcmp(state, "outdated") == 0)
            ts = (double)local(2026, 11, 13, 18, 30);
        snprintf(rec, sizeof(rec), NOV, ts);
        if (cal_observe(rec) != 1) {
            fprintf(stderr, "the built-in record was rejected\n");
            return 1;
        }
    }

    lv_init();
    const int32_t W = UNIT_W_N(2), H = UNIT_H;
    lv_display_t *disp = lv_display_create(W, H);
    static uint8_t buf[UNIT_W_N(2) * 64 * 4];
    lv_display_set_buffers(disp, buf, NULL, sizeof(buf), LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_flush_cb(disp, flush_cb);
    lv_obj_t *scr = lv_screen_active();
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x11111B), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);

    lv_obj_t *card = calendar_card_create(scr);
    lv_obj_set_pos(card, CELL_PAD, CELL_PAD);
    calendar_card_update(card, now);

    lv_draw_buf_t *snap = lv_snapshot_take(scr, LV_COLOR_FORMAT_XRGB8888);
    if (!snap) {
        fprintf(stderr, "snapshot failed\n");
        return 1;
    }
    FILE *f = fopen(argv[1], "wb");
    bool ok = f && bmp_write_xrgb8888(f, snap->data, (int)snap->header.w, (int)snap->header.h,
                                      (int)snap->header.stride);
    if (f)
        fclose(f);
    lv_draw_buf_destroy(snap);
    cal_reset();
    return ok ? 0 : 1;
}
