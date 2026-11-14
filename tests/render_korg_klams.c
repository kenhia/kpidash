/* render_korg_klams.c — offline render of the korg + klams card to BMP
 * (sprint 038). Not a test: a review tool, built with the full native tree (it
 * needs LVGL), so the card can be compared with the mock-up without a Pi.
 *
 *   render_korg_klams <out.bmp> [ok|warn|stale|down|nofeed]
 *   render_korg_klams <out.bmp> live <korg.json> <klams.json> [now]
 *
 * ok, warn and stale are the mock-up's three states (row2-cards-mockups.html,
 * `combo()`, data D.ok / D.warn / D.stale), built as contract records. Where
 * the mock-up's scanner rows disagree with the rules of korg:4618 comment
 * 4672 the rules win, so:
 *   ok     both scanners ran on schedule; kubs0 last WROTE 3 days ago and is
 *          healthy, its write age secondary text (the mock-up drew it red)
 *   warn   klams degraded with rerank missing on two records in a row, and
 *          kai's scanner last ran 5 h ago: stalled, red
 *   stale  the ok records read 14 minutes on: both halves grey
 * and two the mock-up does not draw:
 *   down   korg fresh; klams fresh and `down` on two records in a row
 *   nofeed neither key has ever published
 * live reads two records saved from rpi53 and draws them at `now` (default:
 * the clock). The canvas is the whole 632 x 628 cell, the card at CELL_PAD
 * inside it, on the panel's crust. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "bmp_write.h"
#include "kkstate.h"
#include "layout.h"
#include "lvgl.h"
#include "widgets/korg_klams_card.h"

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

/* The mock-up's series, the newest 30 of its 31 values, ending 2026-11-13. */
static const int HIST_OK[30] = {97,  97,  99,  100, 100, 102, 103, 103, 105, 106,
                                108, 108, 109, 111, 112, 112, 113, 114, 115, 115,
                                116, 117, 117, 118, 118, 61,  18,  21,  22,  25};
static const int HIST_WARN[30] = {18, 19, 19, 21, 22, 22, 23, 23, 24, 25, 25, 26, 27, 27, 28,
                                  29, 30, 30, 31, 33, 34, 34, 35, 36, 37, 38, 39, 40, 40, 41};

/* The projects holding one open item each: only the top project is named on
 * the card, so these are segments, not labels. */
static const char *ONES[12] = {"kai-ops", "kdashdata", "kfdc",     "klams", "korg", "kpidash",
                               "krot",    "kweather",  "kxeneon", "agent-skills", "knarr", "kmon"};

typedef struct {
    const char *name;
    int open;
} proj_t;

static void korg_json(char *buf, size_t n, double ts, int open, int awaiting, int active,
                      const proj_t *proj, int np, int ones, const int *hist) {
    size_t o = (size_t)snprintf(buf, n,
                                "{\"ts\":%.1f,\"expected_refresh_s\":300,\"open\":%d,"
                                "\"projects\":%d,\"awaiting\":%d,\"active\":%d,\"queued\":1,"
                                "\"by_project\":[",
                                ts, open, np + ones, awaiting, active);
    for (int i = 0; i < np + ones; i++)
        o += (size_t)snprintf(buf + o, n - o, "%s{\"name\":\"%s\",\"open\":%d}", i ? "," : "",
                              i < np ? proj[i].name : ONES[i - np], i < np ? proj[i].open : 1);
    o += (size_t)snprintf(buf + o, n - o, "],\"other_open\":0,\"history\":[");
    /* 30 days ending 2026-11-13 (2026-10-15 .. 2026-11-13). */
    for (int i = 0; i < 30; i++) {
        int mon = i < 17 ? 10 : 11, day = i < 17 ? 15 + i : i - 16;
        o += (size_t)snprintf(buf + o, n - o, "%s{\"date\":\"2026-%02d-%02d\",\"open\":%d}",
                              i ? "," : "", mon, day, hist[i]);
    }
    snprintf(buf + o, n - o, "]}");
}

static void klams_json(char *buf, size_t n, double ts, const char *health, double kai_ran,
                       double kai_wrote, double kubs_ran, double kubs_wrote, bool rerank_ok) {
    snprintf(buf, n,
             "{\"ts\":%.1f,\"expected_refresh_s\":60,\"health\":\"%s\",\"version\":\"0.1.58\","
             "\"uptime_s\":57600,\"knowledge\":228934,\"facts\":40,\"events\":39,"
             "\"scanners\":[{\"name\":\"kai\",\"last_wrote_at\":%.0f,\"last_ran_at\":%.0f,"
             "\"expected_interval_s\":3600},{\"name\":\"kubs0\",\"last_wrote_at\":%.0f,"
             "\"last_ran_at\":%.0f,\"expected_interval_s\":3600}],"
             "\"subsystems\":[{\"name\":\"postgres\",\"ok\":true},{\"name\":\"qdrant\",\"ok\":true},"
             "{\"name\":\"embed\",\"ok\":true},{\"name\":\"rerank\",\"ok\":%s}],"
             "\"queue_depth\":0,\"queue_capacity\":1024,\"backup_at\":null,\"new_per_day\":[]}",
             ts, health, kai_wrote, kai_ran, kubs_wrote, kubs_ran, rerank_ok ? "true" : "false");
}

static const char *DOWN_JSON =
    "{\"ts\":%.1f,\"expected_refresh_s\":60,\"health\":\"down\",\"version\":null,"
    "\"uptime_s\":null,\"knowledge\":null,\"facts\":null,\"events\":null,\"scanners\":[],"
    "\"subsystems\":[],\"queue_depth\":null,\"queue_capacity\":null,\"backup_at\":null,"
    "\"new_per_day\":[]}";

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr,
                "usage: %s out.bmp [ok|warn|stale|down|nofeed]\n"
                "       %s out.bmp live korg.json klams.json [now]\n",
                argv[0], argv[0]);
        return 2;
    }
    const char *state = argc > 2 ? argv[2] : "ok";
    setenv("TZ", "America/Los_Angeles", 1);
    tzset();

    static char kj[8192], mj[4096];
    const double t0 = 1794617100; /* 2026-11-13 15:25 PST, the schema examples' moment */
    double now = t0 + 40;
    kk_reset();

    static const proj_t ok_proj[] = {{"kligd", 5}, {"k-homelab", 4}, {"kdeskdash", 2}, {"kloom", 2}};
    static const proj_t warn_proj[] = {{"k-homelab", 9}, {"kligd", 6}, {"kpidash", 4},
                                       {"korg", 3},      {"kloom", 2}, {"klams", 2},
                                       {"kfdc", 2},      {"krot", 2}};

    if (strcmp(state, "live") == 0) {
        if (argc < 5) {
            fprintf(stderr, "live needs korg.json and klams.json\n");
            return 2;
        }
        char *k = slurp(argv[3]), *m = slurp(argv[4]);
        if (!k || !m || kk_observe_korg(k) != 1 || kk_observe_klams(m) != 1) {
            fprintf(stderr, "cannot read the live records\n");
            return 1;
        }
        free(k);
        free(m);
        now = argc > 5 ? atof(argv[5]) : (double)time(NULL);
    } else if (strcmp(state, "warn") == 0) {
        korg_json(kj, sizeof(kj), t0, 41, 4, 2, warn_proj, 8, 11, HIST_WARN);
        kk_observe_korg(kj);
        /* rerank missing on two records in a row; kai last ran 5 h ago. */
        for (int i = 1; i >= 0; i--) {
            klams_json(mj, sizeof(mj), t0 - 60 * i, "degraded", t0 - 5 * 3600, t0 - 5 * 3600,
                       t0 - 180, t0 - 3 * 86400 - 3600, false);
            kk_observe_klams(mj);
        }
    } else if (strcmp(state, "down") == 0) {
        korg_json(kj, sizeof(kj), t0, 25, 1, 0, ok_proj, 4, 12, HIST_OK);
        kk_observe_korg(kj);
        klams_json(mj, sizeof(mj), t0 - 120, "ok", t0 - 540, t0 - 1140, t0 - 180,
                   t0 - 3 * 86400 - 3600, true);
        kk_observe_klams(mj);
        for (int i = 1; i >= 0; i--) {
            snprintf(mj, sizeof(mj), DOWN_JSON, t0 - 60 * i);
            kk_observe_klams(mj);
        }
    } else if (strcmp(state, "nofeed") == 0) {
        /* nothing observed */
    } else {
        korg_json(kj, sizeof(kj), t0, 25, 1, 0, ok_proj, 4, 12, HIST_OK);
        kk_observe_korg(kj);
        /* kai ran 9 min ago (its bar 0.85 full) and wrote 19 min ago; kubs0
         * ran 3 min ago and last wrote 3 days ago. */
        klams_json(mj, sizeof(mj), t0, "ok", t0 - 540, t0 - 1140, t0 - 180,
                   t0 - 3 * 86400 - 3600, true);
        kk_observe_klams(mj);
        if (strcmp(state, "stale") == 0)
            now += 14 * 60;
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

    lv_obj_t *card = korg_klams_card_create(scr);
    lv_obj_set_pos(card, CELL_PAD, CELL_PAD);
    korg_klams_card_update(card, now);

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
    kk_reset();
    return ok ? 0 : 1;
}
