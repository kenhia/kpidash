/* render_limits.c — offline render of the Claude limits card to BMP (sprint
 * 037). Not a test: a review tool, built with the full native tree (it needs
 * LVGL), so the card can be compared with the mock-up without a Pi.
 *
 *   render_limits <out.bmp> [ok|warn|stale|bare|live037|nofive-ok|nofive-warn|
 *                            nofive-stale|nofive-long|seven|live]
 *
 * ok, warn and stale are the mock-up's three states (row2-cards-mockups.html,
 * `limits()`, data D.ok / D.warn / D.stale), built as claude:limits hashes
 * whose reset times put each pace tick where the mock-up draws it:
 *   ok     cleo, 40 s old: 42 % / 61 % / Fable 38 %, all peach
 *   warn   kai, 2 m old: 91 % (red), 78 % (yellow), Fable 84 % (yellow)
 *   stale  the ok record read 22 minutes on: every gauge grey
 * and two more the mock-up does not draw:
 *   bare   a statusline write with no scoped set: no bar
 *   live037 the record kai published on 2026-11-14 00:42Z, read 47 s on
 * Sprint 039 (WI #4631) adds the layouts for a record that says there is no
 * five-hour window (five_hour_none 1), which no mock-up draws:
 *   nofive-ok     7 DAY 61 % and FABLE · 7 DAY 38 %, both peach, ticks at 0.72
 *   nofive-warn   7 DAY 78 % (yellow), Fable 91 % (red)
 *   nofive-stale  nofive-ok read 22 minutes on: both rings grey
 *   seven         no five-hour window and no scoped set: one 7 DAY ring
 *   nofive-long   nofive-ok with the schema's example model, "claude-opus-5",
 *                 whose ring label is too wide with the window: model alone
 *   live          the record kai published at 2026-11-14 02:44Z, read 47 s on
 * Times are drawn in America/Los_Angeles, rpi53's zone. The canvas is the
 * whole 632 x 628 cell, the card at CELL_PAD inside it, on the panel's crust. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "bmp_write.h"
#include "layout.h"
#include "limstate.h"
#include "lvgl.h"
#include "widgets/limits_card.h"

static void flush_cb(lv_display_t *d, const lv_area_t *a, uint8_t *px) {
    (void)a;
    (void)px;
    lv_display_flush_ready(d);
}

static time_t local_time(int y, int mo, int d, int h, int mi) {
    struct tm t = {0};
    t.tm_year = y - 1900;
    t.tm_mon = mo - 1;
    t.tm_mday = d;
    t.tm_hour = h;
    t.tm_min = mi;
    t.tm_isdst = -1;
    return mktime(&t);
}

#define MAXF 20
static const char *F[MAXF], *V[MAXF];
static char store[MAXF][32];
static int nf;
static const char *g_model = "Fable";

static void put(const char *f, const char *fmt, double v) {
    snprintf(store[nf], sizeof(store[nf]), fmt, v);
    F[nf] = f;
    V[nf] = store[nf];
    nf++;
}

static void put_s(const char *f, const char *v) {
    F[nf] = f;
    V[nf] = v;
    nf++;
}

/* A record whose rings and bar sit at `p5`/`p7`/`ps` percent, with each pace
 * tick at `k5`/`k7`/`ks` of its window, observed `age` seconds before `now`. */
static void build(double now, double age, const char *host, double p5, double k5, double p7,
                  double k7, double ps, double ks, bool scoped, double five_reset) {
    nf = 0;
    double obs = now - age;
    put("five_hour_pct", "%.0f", p5 < 0 ? 0 : p5);
    put("seven_day_pct", "%.0f", p7);
    if (p5 < 0) /* no five-hour window, as CD-29's writers publish it */
        put("five_hour_none", "%.0f", 1);
    put("five_hour_resets_at", "%.0f",
        p5 < 0 ? 0 : five_reset > 0 ? five_reset : now + (1 - k5) * LIM_FIVE_HOUR_S);
    put("seven_day_resets_at", "%.0f", now + (1 - k7) * LIM_SEVEN_DAY_S);
    put("updated_at", "%.0f", obs);
    put("expected_refresh_s", "%.0f", 300);
    put_s("host", host);
    if (scoped) {
        put_s("scoped_model", g_model);
        put("scoped_pct", "%.0f", ps);
        put("scoped_resets_at", "%.0f", now + (1 - ks) * LIM_SEVEN_DAY_S);
        put("scoped_updated_at", "%.0f", obs);
        put("scoped_expected_refresh_s", "%.0f", 300);
    }
}

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "usage: %s out.bmp [ok|warn|stale|bare|live037|nofive-ok|nofive-warn|nofive-stale|nofive-long|seven|live]\n", argv[0]);
        return 2;
    }
    const char *state = argc > 2 ? argv[2] : "ok";
    setenv("TZ", "America/Los_Angeles", 1);
    tzset();

    /* The 7-day ring resets "Mon 09:00", as in the mock-up; `now` is placed
     * so its tick lands at the mock-up's fraction. */
    const double mon = (double)local_time(2026, 11, 16, 9, 0);
    double now;
    lim_reset();
    if (strcmp(state, "warn") == 0) {
        now = mon - (1 - 0.60) * LIM_SEVEN_DAY_S;
        /* "resets in 2h 36m": 1 - 156/300 = 0.48, the mock-up's tick. */
        build(now, 120, "kai", 91, 0, 78, 0.60, 84, 0.60, true, now + 156 * 60 - 30);
    } else if (strncmp(state, "nofive-", 7) == 0 || strcmp(state, "seven") == 0) {
        bool warn = strcmp(state, "nofive-warn") == 0;
        if (strcmp(state, "nofive-long") == 0)
            g_model = "claude-opus-5";
        now = mon - (1 - 0.72) * LIM_SEVEN_DAY_S;
        if (warn)
            build(now, 120, "kai", -1, 0, 78, 0.72, 91, 0.72, true, 0);
        else
            build(now, 40, "kai", -1, 0, 61, 0.72, 38, 0.72, strcmp(state, "seven") != 0, 0);
        if (strcmp(state, "nofive-stale") == 0)
            now += 22 * 60;
    } else if (strcmp(state, "live") == 0) {
        static const char *lf[] = {
            "five_hour_pct", "seven_day_pct", "updated_at", "host", "source",
            "expected_refresh_s", "ts", "five_hour_resets_at", "seven_day_resets_at",
            "scoped_model", "scoped_pct", "scoped_active", "scoped_count", "scoped_updated_at",
            "scoped_expected_refresh_s", "scoped_resets_at", "five_hour_none",
        };
        static const char *lv[] = {
            "0", "11", "1794621843", "kai", "oauth", "300", "1794621844", "0",
            "1795089599", "Fable", "13", "1", "1", "1794621843", "300", "1795089599", "1",
        };
        nf = (int)(sizeof(lf) / sizeof(lf[0]));
        memcpy(F, lf, sizeof(lf));
        memcpy(V, lv, sizeof(lv));
        now = 1794621843 + 47;
    } else if (strcmp(state, "live037") == 0) {
        static const char *lf[] = {
            "five_hour_pct", "seven_day_pct", "updated_at", "host", "source",
            "expected_refresh_s", "ts", "five_hour_resets_at", "seven_day_resets_at",
            "scoped_model", "scoped_pct", "scoped_active", "scoped_count", "scoped_updated_at",
            "scoped_expected_refresh_s", "scoped_resets_at",
        };
        static const char *lv[] = {
            "8", "8", "1794616923", "kai", "oauth", "300", "1794616924", "1795089599",
            "1795089599", "Fable", "11", "1", "1", "1794616923", "300", "1795089599",
        };
        nf = (int)(sizeof(lf) / sizeof(lf[0]));
        memcpy(F, lf, sizeof(lf));
        memcpy(V, lv, sizeof(lv));
        now = 1794616923 + 47;
    } else {
        now = mon - (1 - 0.72) * LIM_SEVEN_DAY_S;
        bool stale = strcmp(state, "stale") == 0;
        bool bare = strcmp(state, "bare") == 0;
        /* "resets in 2h 14m": 1 - 134/300 = 0.553, the mock-up's 0.55. */
        build(now, 40, "cleo", 42, 0, 61, 0.72, 38, 0.72, !bare, now + 134 * 60 - 30);
        if (stale)
            now += 22 * 60;
    }
    if (lim_observe(F, V, nf, now) != 1) {
        fprintf(stderr, "cannot build the %s record\n", state);
        return 1;
    }

    lv_init();
    const int32_t W = UNIT_W, H = UNIT_H;
    lv_display_t *disp = lv_display_create(W, H);
    static uint8_t buf[UNIT_W * 64 * 4];
    lv_display_set_buffers(disp, buf, NULL, sizeof(buf), LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_flush_cb(disp, flush_cb);
    lv_obj_t *scr = lv_screen_active();
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x11111B), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);

    lv_obj_t *card = limits_card_create(scr);
    lv_obj_set_pos(card, CELL_PAD, CELL_PAD);
    limits_card_update(card, now);

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
    return ok ? 0 : 1;
}
