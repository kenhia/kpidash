/* render_limits.c — offline render of the usage card to BMP (sprint 037; the
 * triangle and Codex, sprint 049). Not a test: a review tool, built with the
 * full native tree (it needs LVGL), so the card can be checked without a Pi.
 *
 *   render_limits <out.bmp> [state]
 *
 * Sprint 049 (WI #4941) replaced the earlier states with the triangle's:
 *   ok           Claude 61 %, Fable 38 % (resetting with Claude: one reset
 *                line), Codex 24 % in its green; everything fresh
 *   split        as ok, but Fable resets a day after Claude: both lines
 *   warn         Claude 78 % (yellow), Fable 91 % (red), Codex 82 % (yellow)
 *   codex-high   Claude and Fable as ok, Codex 93 % (red)
 *   codex-stale  as ok, but Codex last observed 22 minutes ago: grey ring,
 *                "codex: no data 22m" in the header
 *   stale        as ok, every record 22 minutes old: all grey
 *   claude-only  no Codex record ever: the bottom slot is empty
 *   codex-only   no Claude record ever: only the bottom ring
 *   live         the records rpi53 held at 2026-11-25 21:50Z, read 47 s on
 *   gauge <c7> <fable> <codex> [pace]
 *                any three percentages, ticks at `pace` (default 0.72)
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

/* A claude:limits record as kai's oauth poll writes it on Ken's plan (no
 * five-hour window): 7-day at `p7`, Fable at `ps`, both resetting at `reset`
 * unless `fable_reset` says otherwise, observed `age` seconds before `now`. */
static void claude(double now, double age, double p7, double ps, double reset,
                   double fable_reset) {
    nf = 0;
    double obs = now - age;
    put("five_hour_pct", "%.0f", 0);
    put("five_hour_none", "%.0f", 1);
    put("five_hour_resets_at", "%.0f", 0);
    put("seven_day_pct", "%.0f", p7);
    put("seven_day_resets_at", "%.0f", reset);
    put("updated_at", "%.0f", obs);
    put("expected_refresh_s", "%.0f", 300);
    put_s("host", "kai");
    put_s("scoped_model", "Fable");
    put("scoped_pct", "%.0f", ps);
    put("scoped_resets_at", "%.0f", fable_reset > 0 ? fable_reset : reset);
    put("scoped_updated_at", "%.0f", obs);
    put("scoped_expected_refresh_s", "%.0f", 300);
}

/* A codex:limits record as kdeskdash's codex-pub writes it on Pro. */
static void codex(double now, double age, double p7, double reset) {
    nf = 0;
    put("five_hour_none", "%.0f", 1);
    put("five_hour_pct", "%.0f", 0);
    put("five_hour_resets_at", "%.0f", 0);
    put("seven_day_pct", "%.0f", p7);
    put("seven_day_resets_at", "%.0f", reset);
    put("updated_at", "%.0f", now - age);
    put("expected_refresh_s", "%.0f", 300);
    put_s("host", "kai");
    put_s("source", "app-server");
    put_s("plan_type", "prolite");
}

static int feed_claude(void) {
    return lim_observe(F, V, nf);
}

static int feed_codex(void) {
    return lim_codex_observe(F, V, nf);
}

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr,
                "usage: %s out.bmp [ok|split|warn|codex-high|codex-stale|stale|claude-only|"
                "codex-only|live|gauge c7 fable codex [pace]]\n",
                argv[0]);
        return 2;
    }
    const char *state = argc > 2 ? argv[2] : "ok";
    setenv("TZ", "America/Los_Angeles", 1);
    tzset();

    /* Claude's week resets Mon 09:00 and Codex's Mon 21:50 (its real weekly
     * reset), `now` placed so the Claude ticks sit at 0.72. */
    const double mon = (double)local_time(2026, 11, 30, 9, 0);
    const double cx_reset = (double)local_time(2026, 11, 30, 21, 50);
    double now = mon - (1 - 0.72) * LIM_SEVEN_DAY_S;
    lim_reset();
    int built = 1;
    if (strcmp(state, "live") == 0) {
        static const char *lf[] = {
            "five_hour_pct", "seven_day_pct", "updated_at", "host", "source",
            "expected_refresh_s", "ts", "five_hour_resets_at", "seven_day_resets_at",
            "scoped_model", "scoped_pct", "scoped_active", "scoped_count", "scoped_updated_at",
            "scoped_expected_refresh_s", "scoped_resets_at", "five_hour_none",
        };
        static const char *lv[] = {
            "0", "37", "1795643204", "kai", "oauth", "300", "1795643205", "0", "1795694399",
            "Fable", "30", "0", "1", "1795643204", "300", "1795694399", "1",
        };
        static const char *cf[] = {
            "five_hour_none", "five_hour_pct", "five_hour_resets_at", "seven_day_pct",
            "seven_day_resets_at", "updated_at", "expected_refresh_s", "host", "source",
            "plan_type", "ts",
        };
        static const char *cv[] = {
            "1", "0", "0", "1", "1796175971", "1795643144", "300", "kai", "app-server",
            "prolite", "1795643145",
        };
        built = lim_observe(lf, lv, (int)(sizeof(lf) / sizeof(lf[0]))) == 1 &&
             lim_codex_observe(cf, cv, (int)(sizeof(cf) / sizeof(cf[0]))) == 1;
        now = 1795643204 + 47;
    } else if (strcmp(state, "gauge") == 0) {
        if (argc < 6) {
            fprintf(stderr, "gauge <c7> <fable> <codex> [pace]\n");
            return 2;
        }
        double k = argc > 6 ? atof(argv[6]) : 0.72;
        now = mon - (1 - k) * LIM_SEVEN_DAY_S;
        claude(now, 40, atof(argv[3]), atof(argv[4]), mon, 0);
        built = feed_claude() == 1;
        codex(now, 40, atof(argv[5]), now + (1 - k) * LIM_SEVEN_DAY_S);
        built = built && feed_codex() == 1;
    } else {
        bool warn = strcmp(state, "warn") == 0;
        bool high = strcmp(state, "codex-high") == 0;
        double age_c = 40, age_x = 40;
        if (strcmp(state, "stale") == 0)
            age_c = age_x = 22 * 60;
        if (strcmp(state, "codex-stale") == 0)
            age_x = 22 * 60;
        if (strcmp(state, "codex-only") != 0) {
            claude(now, age_c, warn ? 78 : 61, warn ? 91 : 38, mon,
                   strcmp(state, "split") == 0 ? mon + 86400 : 0);
            built = feed_claude() == 1;
        }
        if (built && strcmp(state, "claude-only") != 0) {
            codex(now, age_x, warn ? 82 : high ? 93 : 24, cx_reset);
            built = feed_codex() == 1;
        }
    }
    if (!built) {
        fprintf(stderr, "cannot build the %s records\n", state);
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
