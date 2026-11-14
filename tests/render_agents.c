/* render_agents.c — offline render of the Agents card to BMP (sprint 041).
 * Not a test: a review tool, built with the full native tree (it needs LVGL),
 * so the card can be compared with the mock-up without a Pi.
 *
 *   render_agents <out.bmp> [mock|empty|nofeed|stale|crowded|legs|long]
 *   render_agents <out.bmp> live <agents.json> [unix-time]
 *
 *   mock     the mock-up's day (agents-mockups.html, `S`) at 18:42 on
 *            2026-11-13: two overseen programs, a working and a waiting bar
 *   empty    a record with nothing in the last 24 hours
 *   nofeed   the key has never published: the grid and "no agents feed"
 *   stale    the mock-up's record read 25 minutes after its ts: the running
 *            bars stop at their own end, and "as of 25m" in the header
 *   crowded  30 overlapping bars: rows shrink to the 20 px label's minimum,
 *            then the oldest are left out and counted in the header
 *   legs     a working overseer with two running legs (and a finished one)
 *   long     names at the contract's 31-character bound, one in Cyrillic
 *            (no glyphs in the panel's face: drawn as '?'), cut to fit
 * live reads a record from a file and draws it at the given time (default:
 * the clock). The zone is pinned to America/Los_Angeles, rpi53's. The canvas
 * is the card's whole 2528 × 628 frame, the card at CELL_PAD inside it, on
 * the panel's crust.
 *
 * Every state checks that nothing is drawn right of the now line inside the
 * plot (no bar or label runs into the future) except a ship's diamond. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "agstate.h"
#include "bmp_write.h"
#include "layout.h"
#include "lvgl.h"
#include "widgets/agents_card.h"

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

typedef struct {
    const char *id, *host, *name;
    double a, b; /* hours from local midnight on the 13th */
    const char *state;
    int shipped, overseer;
    const char *parent;
} spec_t;

static time_t g_midnight;

static int build(char *buf, size_t cap, double ts, const spec_t *s, int n) {
    size_t k = (size_t)snprintf(buf, cap,
                                "{\"ts\":%.1f,\"expected_refresh_s\":60,\"window_s\":86400,\"bars\":[",
                                ts);
    for (int i = 0; i < n && k < cap; i++) {
        char parent[64] = "";
        if (s[i].parent)
            snprintf(parent, sizeof(parent), ",\"parent\":\"%s\"", s[i].parent);
        k += (size_t)snprintf(buf + k, cap - k,
                              "%s{\"id\":\"%s\",\"host\":\"%s\",\"name\":\"%s\",\"start\":%.1f,"
                              "\"end\":%.1f,\"state\":\"%s\",\"shipped\":%s,\"overseer\":%s%s}",
                              i ? "," : "", s[i].id, s[i].host, s[i].name,
                              (double)g_midnight + s[i].a * 3600, (double)g_midnight + s[i].b * 3600,
                              s[i].state, s[i].shipped ? "true" : "false",
                              s[i].overseer ? "true" : "false", parent);
    }
    if (k < cap)
        snprintf(buf + k, cap - k, "]}");
    return k < cap;
}

#define NOW_H 18.7
/* The mock-up's day (agents-mockups.html `S`); "other" hosts are named. */
static const spec_t MOCK[] = {
    {"o1", "cleo", "kpidash-refresh", -5.0, 1.7, "done", 0, 1, NULL},
    {"b1", "kubs0", "klams", -3.6, -2.3, "done", 1, 0, "o1"},
    {"b2", "kai", "kdashdata", -2.2, -0.9, "done", 1, 0, "o1"},
    {"b3", "kai", "korg", -0.7, 0.4, "done", 1, 0, "o1"},
    {"b4", "kubs0", "k-homelab", -0.4, 1.1, "done", 1, 0, "o1"},
    {"b5", "kai", "kpidash", 0.5, 1.6, "done", 1, 0, "o1"},
    {"b6", "kimac", "kloom", -3.9, -1.8, "done", 0, 0, NULL},
    {"b7", "kai", "korg", 6.0, 7.4, "done", 0, 0, NULL},
    {"b8", "thrall", "kpong", 7.2, 8.6, "done", 1, 0, NULL},
    {"o2", "cleo", "low-hanging-fruit", 9.0, 14.5, "done", 0, 1, NULL},
    {"c1", "kai", "kmon", 9.3, 10.4, "done", 1, 0, "o2"},
    {"c2", "kubs0", "agent-skills", 9.9, 11.9, "done", 1, 0, "o2"},
    {"c3", "kai", "kfo", 10.6, 11.5, "done", 1, 0, "o2"},
    {"c4", "kai", "kweather", 11.8, 13.3, "done", 1, 0, "o2"},
    {"c5", "kubs0", "k-homelab", 12.4, 14.0, "done", 1, 0, "o2"},
    {"b9", "cleo", "kctrldeck", 14.8, 16.4, "done", 1, 0, NULL},
    {"ba", "cleo", "kvimh", 15.3, NOW_H, "working", 0, 0, NULL},
    {"bb", "kai", "kloc", 16.6, 17.6, "done", 0, 0, NULL},
    {"bc", "cleo", "khow", 17.1, NOW_H, "waiting", 0, 0, NULL},
};

static const spec_t LEGS[] = {
    {"o", "cleo", "kpidash Agents card", 13.5, NOW_H, "working", 0, 1, NULL},
    {"l0", "kai", "kdashdata", 13.8, 15.2, "done", 1, 0, "o"},
    {"l1", "kai", "kpidash", 15.6, NOW_H, "working", 0, 0, "o"},
    {"l2", "kubs0", "ksitrep", 15.9, NOW_H, "working", 0, 0, "o"},
    {"x1", "kai", "korg", 8.0, 10.5, "done", 1, 0, NULL},
    {"x2", "cleo", "staff-meeting", 11.0, 12.6, "done", 0, 0, NULL},
};

/* Names at 31 characters. The Cyrillic one has no glyphs in the panel's
 * bold 20 face: it must draw as '?', not boxes, and still be cut to fit. */
static const spec_t LONG[] = {
    {"o", "cleo", "WWWWWWWWWWWWWWWWWWWWWWWWWWWWWWW", 2.0, 9.0, "done", 0, 1, NULL},
    {"a", "kai", "kpidash agents card row three x", 2.5, 4.0, "done", 1, 0, "o"},
    {"b", "kubs0",
     "\xD0\x9F\xD1\x80\xD0\xB8\xD0\xB2\xD0\xB5\xD1\x82 \xD0\xBC\xD0\xB8\xD1\x80 "
     "kdashdata contract",
     4.5, 8.0, "done", 1, 0, "o"},
    {"c", "thrall", "Caf\xC3\xA9 menu \xE2\x80\x94 d\xC3\xAD" "a dos", 10.0, 13.0, "done", 0, 0, NULL},
    {"d", "kai", "low-hanging-fruit sweep, round 2", 13.5, 14.2, "done", 1, 0, NULL},
    {"e", "kimac", "abcdefghijklmnopqrstuvwxyz01234", 14.6, NOW_H, "waiting", 0, 0, NULL},
};

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr,
                "usage: %s out.bmp [mock|empty|nofeed|stale|crowded|legs|long]\n"
                "       %s out.bmp live agents.json [unix-time]\n",
                argv[0], argv[0]);
        return 2;
    }
    const char *state = argc > 2 ? argv[2] : "mock";
    setenv("TZ", "America/Los_Angeles", 1);
    tzset();
    ag_reset();
    g_midnight = local(2026, 11, 13, 0, 0);

    static char rec[65536];
    time_t now = local(2026, 11, 13, 18, 42);
    int ok = 1;
    if (strcmp(state, "live") == 0) {
        if (argc < 4) {
            fprintf(stderr, "live needs agents.json\n");
            return 2;
        }
        char *s = slurp(argv[3]);
        if (!s || ag_observe(s) != 1) {
            fprintf(stderr, "cannot read the record\n");
            return 1;
        }
        free(s);
        now = argc > 4 ? (time_t)atoll(argv[4]) : time(NULL);
    } else if (strcmp(state, "nofeed") == 0) {
        /* nothing observed */
    } else if (strcmp(state, "empty") == 0) {
        ok = build(rec, sizeof(rec), (double)now - 20, NULL, 0) && ag_observe(rec) == 1;
    } else if (strcmp(state, "stale") == 0) {
        ok = build(rec, sizeof(rec), (double)now, MOCK, (int)(sizeof(MOCK) / sizeof(MOCK[0]))) &&
             ag_observe(rec) == 1;
        now += 25 * 60;
    } else if (strcmp(state, "crowded") == 0) {
        static spec_t S[30];
        static char ids[30][8], names[30][24];
        static const char *const HOSTS[4] = {"cleo", "kai", "kubs0", "kimac"};
        for (int i = 0; i < 30; i++) {
            snprintf(ids[i], sizeof(ids[i]), "x%d", i);
            snprintf(names[i], sizeof(names[i]), "session-%02d", i);
            S[i] = (spec_t){ids[i], HOSTS[i % 4], names[i], 4.0 + i * 0.35, 9.0 + i * 0.3,
                            i % 7 == 6 ? "working" : "done", i % 3 == 0, 0, NULL};
            if (i >= 27)
                S[i].b = NOW_H;
        }
        ok = build(rec, sizeof(rec), (double)now, S, 30) && ag_observe(rec) == 1;
    } else if (strcmp(state, "legs") == 0) {
        ok = build(rec, sizeof(rec), (double)now, LEGS, (int)(sizeof(LEGS) / sizeof(LEGS[0]))) &&
             ag_observe(rec) == 1;
    } else if (strcmp(state, "long") == 0) {
        ok = build(rec, sizeof(rec), (double)now, LONG, (int)(sizeof(LONG) / sizeof(LONG[0]))) &&
             ag_observe(rec) == 1;
    } else {
        ok = build(rec, sizeof(rec), (double)now, MOCK, (int)(sizeof(MOCK) / sizeof(MOCK[0]))) &&
             ag_observe(rec) == 1;
    }
    if (!ok) {
        fprintf(stderr, "the built-in record was rejected\n");
        return 1;
    }

    lv_init();
    const int32_t W = UNIT_W_N(4), H = UNIT_H;
    lv_display_t *disp = lv_display_create(W, H);
    static uint8_t buf[UNIT_W_N(4) * 64 * 4];
    lv_display_set_buffers(disp, buf, NULL, sizeof(buf), LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_flush_cb(disp, flush_cb);
    lv_obj_t *scr = lv_screen_active();
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x11111B), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);

    lv_obj_t *card = agents_card_create(scr);
    lv_obj_set_pos(card, CELL_PAD, CELL_PAD);
    agents_card_update(card, now);

    lv_draw_buf_t *snap = lv_snapshot_take(scr, LV_COLOR_FORMAT_XRGB8888);
    if (!snap) {
        fprintf(stderr, "snapshot failed\n");
        return 1;
    }

    /* Nothing right of the now line inside the plot but Base, except where a
     * ship's diamond (centred on its bar's end) reaches over. */
    static ag_view_t v;
    ag_view(now, &v);
    int stray = 0;
    for (int y = AG_ROW_TOP; y < AG_ROW_BOT; y++) {
        bool ship_row = false;
        for (int i = 0; i < v.nbars; i++)
            if (v.bars[i].shipped && v.bars[i].x1 > AG_PLOT_X1 - 20 && y >= v.bars[i].y - 4 &&
                y < v.bars[i].y + v.bars[i].h + 4)
                ship_row = true;
        if (ship_row)
            continue;
        for (int x = AG_PLOT_X1 + 2; x < AG_FRAME_W - 12; x++) {
            const uint8_t *p = snap->data + (size_t)y * snap->header.stride + (size_t)x * 4;
            if (p[0] != 0x2E || p[1] != 0x1E || p[2] != 0x1E) /* B, G, R of Base */
                stray++;
        }
    }
    if (stray)
        fprintf(stderr, "%d pixels drawn right of the now line\n", stray);

    FILE *f = fopen(argv[1], "wb");
    bool wrote = f && bmp_write_xrgb8888(f, snap->data, (int)snap->header.w,
                                         (int)snap->header.h, (int)snap->header.stride);
    if (f)
        fclose(f);
    lv_draw_buf_destroy(snap);
    ag_reset();
    return wrote && !stray ? 0 : 1;
}
