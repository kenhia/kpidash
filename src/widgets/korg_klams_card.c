/**
 * korg_klams_card.c — korg and klams sharing one 1×1 (WI #4613). See the header.
 *
 * Coordinates are the mock-up's (sprints/planning/row2-cards-mockups.html,
 * `combo()`), in its 632 × 628 cell, at real panel pixels. The object sits
 * CELL_PAD inside that cell, so every draw call offsets by the cell's origin.
 * Text y values are the SVG's baselines, which text_at() converts to LVGL's
 * top-of-line.
 *
 * Where the mock-up's data and today's records part company, the rules of
 * korg:4618 comment 4672 win: a scanner is judged by when it last RAN, the
 * write age is secondary text, and a scanner with no run time is neutral,
 * never red.
 */
#include "korg_klams_card.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "common.h"
#include "kkstate.h"
#include "layout.h"

/* Mocha colours the palette does not carry yet. */
#define KK_CRUST lv_color_hex(0x11111B)
#define KK_SUBTEXT0 lv_color_hex(0xA6ADC8)

#define FONT_BIG (&lv_font_montserrat_bold_96_digits)
#define FONT_OPEN (&lv_font_montserrat_36)
#define FONT_LANE (&lv_font_montserrat_bold_36)
#define FONT_TITLE (&lv_font_montserrat_bold_28)
#define FONT_NAME (&lv_font_montserrat_bold_28)
#define FONT_CHUNKS (&lv_font_montserrat_28)
#define FONT_NOTE (&lv_font_montserrat_bold_24)
#define FONT_TEXT (&lv_font_montserrat_24)
#define FONT_TEXT_B (&lv_font_montserrat_bold_24)

/* ---- Geometry (mock-up `combo`) ---- */
#define LEFT 36
#define RIGHT 596
#define INNER_W (RIGHT - LEFT)
#define KORG_BASE 62
#define OPEN_BASE 170
#define SPARK_X 300 /* the line's left edge, moved right of a wide count */
#define SPARK_Y 84
#define SPARK_H 90
#define LANE_BASE 238
#define PBAR_Y 262
#define PBAR_H 22
#define PBAR_GAP 4
#define DIVIDER_Y 310
#define KLAMS_BASE 366
#define SCAN_BASE 432
#define SCAN_DY 58
#define SCAN_NAME_W 110 /* "kubs0" in bold 28, and a gap */
#define SCAN_BAR_X (LEFT + SCAN_NAME_W)
#define SCAN_TEXT_W 210 /* "wrote 59m ago" at 24 px, and a gap */
#define SCAN_BAR_W (INNER_W - SCAN_NAME_W - SCAN_TEXT_W)
#define SCAN_BAR_H 18
#define SUBS_BASE 574
#define HALF_Y 316 /* where the klams half's repaint starts, in the cell */

#define KK_NAME_MAX 48

typedef struct {
    bool have;
    bool stale;
    char note[24]; /* "no data 14m" / "no feed" */
    int open, awaiting, active;
    int npts;
    kk_point_t pts[KDASH_KORG_HISTORY_MAX];
    char top[KK_NAME_MAX + 8];
    int nseg;
    int seg[KDASH_KORG_PROJECTS_MAX + 1];
    bool seg_other; /* the last segment is other_open */
} korg_view_t;

typedef struct {
    bool have;
    bool stale;
    char note[24];
    kdash_klams_health_t health;
    char chunks[32];
    int nscan;
    char scan_name[KK_SCANNER_ROWS][KDASH_KLAMS_NAME_MAX];
    kk_scan_state_t scan_state[KK_SCANNER_ROWS];
    int scan_px[KK_SCANNER_ROWS]; /* the bar's fill, in pixels */
    char scan_text[KK_SCANNER_ROWS][48];
    int nsub;
    char sub_name[KDASH_KLAMS_SUBSYSTEMS_MAX][KDASH_KLAMS_NAME_MAX];
    bool sub_down[KDASH_KLAMS_SUBSYSTEMS_MAX];
} klams_view_t;

typedef struct {
    korg_view_t k;
    klams_view_t m;
    bool valid;
} view_t;

typedef struct {
    view_t cur;
    view_t next; /* scratch, zeroed each update so memcmp sees only content */
} kk_widget_t;

/* An opaque colour `t` of the way from `a` to `b` (sprint 030: never blend). */
static lv_color_t mixc(lv_color_t a, lv_color_t b, double t) {
    return lv_color_mix(b, a, (uint8_t)lround(t * 255));
}

/* ---- Drawing helpers ---- */

static void rect(lv_layer_t *layer, int32_t x, int32_t y, int32_t w, int32_t h, lv_color_t c,
                 int32_t radius) {
    if (w <= 0 || h <= 0)
        return;
    lv_draw_rect_dsc_t d;
    lv_draw_rect_dsc_init(&d);
    d.bg_color = c;
    d.bg_opa = LV_OPA_COVER;
    d.radius = radius;
    lv_area_t a = {x, y, x + w - 1, y + h - 1};
    lv_draw_rect(layer, &d, &a);
}

static void line(lv_layer_t *layer, int32_t x1, int32_t y1, int32_t x2, int32_t y2, lv_color_t c,
                 int32_t w) {
    lv_draw_line_dsc_t d;
    lv_draw_line_dsc_init(&d);
    d.color = c;
    d.width = w;
    d.opa = LV_OPA_COVER;
    d.round_start = 1;
    d.round_end = 1;
    d.p1.x = x1;
    d.p1.y = y1;
    d.p2.x = x2;
    d.p2.y = y2;
    lv_draw_line(layer, &d);
}

static void dot(lv_layer_t *layer, int32_t cx, int32_t cy, int32_t r, lv_color_t c) {
    rect(layer, cx - r, cy - r, 2 * r + 1, 2 * r + 1, c, LV_RADIUS_CIRCLE);
}

static int32_t text_w(const lv_font_t *font, const char *s) {
    return lv_text_get_width(s, (uint32_t)strlen(s), font, 0);
}

/* Text whose BASELINE is at y. `x` is the left edge, or the right edge for
 * LV_TEXT_ALIGN_RIGHT. Returns the width drawn. */
static int32_t text_at(lv_layer_t *layer, const lv_font_t *font, int32_t x, int32_t baseline,
                       const char *s, lv_color_t c, lv_text_align_t align) {
    if (!s || !s[0])
        return 0;
    int32_t w = text_w(font, s);
    if (align == LV_TEXT_ALIGN_RIGHT)
        x -= w;
    lv_draw_label_dsc_t d;
    lv_draw_label_dsc_init(&d);
    d.text = s;
    d.text_local = 1; /* callers pass stack buffers; the draw task outlives them */
    d.font = font;
    d.color = c;
    d.opa = LV_OPA_COVER;
    int32_t top = baseline - (font->line_height - font->base_line);
    lv_area_t a = {x, top, x + w, top + font->line_height - 1};
    lv_draw_label(layer, &d, &a);
    return w;
}

/* ---- korg ---- */

static void draw_spark(lv_layer_t *layer, int32_t ox, int32_t oy, int32_t x0, const korg_view_t *k,
                       lv_color_t col) {
    if (k->npts < 2)
        return; /* a short or empty history draws no line; the count keeps its place */
    const int32_t w = ox + RIGHT - x0, y0 = oy + SPARK_Y, bottom = y0 + SPARK_H;
    int32_t px[KDASH_KORG_HISTORY_MAX], py[KDASH_KORG_HISTORY_MAX];
    for (int i = 0; i < k->npts; i++) {
        px[i] = x0 + (int32_t)lround(k->pts[i].x * w);
        py[i] = bottom - (int32_t)lround(k->pts[i].y * SPARK_H);
    }
    /* The area under the line, one opaque column per pixel, pre-mixed. */
    const lv_color_t fill = mixc(WS_COLOR_BG, col, 0.2);
    for (int i = 0; i + 1 < k->npts; i++) {
        for (int32_t x = px[i]; x < px[i + 1]; x++) {
            double t = (double)(x - px[i]) / (px[i + 1] - px[i]);
            int32_t y = (int32_t)lround(py[i] + t * (py[i + 1] - py[i]));
            rect(layer, x, y, 1, bottom - y, fill, 0);
        }
    }
    for (int i = 0; i + 1 < k->npts; i++)
        line(layer, px[i], py[i], px[i + 1], py[i + 1], col, 4);
    dot(layer, px[k->npts - 1], py[k->npts - 1], 8, col);
}

static void draw_projbar(lv_layer_t *layer, int32_t ox, int32_t oy, const korg_view_t *k) {
    const int32_t x0 = ox + LEFT, y = oy + PBAR_Y;
    int tot = 0;
    for (int i = 0; i < k->nseg; i++)
        tot += k->seg[i];
    if (tot <= 0) {
        rect(layer, x0, y, INNER_W, PBAR_H, WS_COLOR_SURFACE0, 4);
        return;
    }
    const double unit = (double)(INNER_W - PBAR_GAP * (k->nseg - 1)) / tot;
    double cx = x0;
    for (int i = 0; i < k->nseg; i++) {
        lv_color_t c;
        bool other = k->seg_other && i == k->nseg - 1;
        if (k->stale)
            c = i == 0 ? WS_COLOR_SURFACE1 : WS_COLOR_SURFACE0;
        else if (other)
            c = WS_COLOR_SURFACE0; /* the remainder no segment shows: muted */
        else if (i == 0)
            c = WS_COLOR_LAVENDER;
        else if (i < 4)
            c = mixc(WS_COLOR_BG, WS_COLOR_LAVENDER, 0.5);
        else
            c = WS_COLOR_SURFACE1;
        double sw = k->seg[i] * unit;
        int32_t xa = (int32_t)lround(cx), xb = (int32_t)lround(cx + sw);
        rect(layer, xa, y, xb - xa < 1 ? 1 : xb - xa, PBAR_H, c, 4);
        cx += sw + PBAR_GAP;
    }
}

/* The top project's "name count", right-aligned, its name cut until it clears
 * the lane counts on its left. */
static void draw_top(lv_layer_t *layer, int32_t ox, int32_t oy, int32_t min_x, const char *top,
                     lv_color_t c) {
    char buf[KK_NAME_MAX + 8];
    snprintf(buf, sizeof(buf), "%s", top);
    const int32_t avail = ox + RIGHT - min_x;
    if (text_w(FONT_NAME, buf) > avail) {
        /* top is "name count": shorten the name, keep the count. */
        char *sp = strrchr(buf, ' ');
        char count[16];
        snprintf(count, sizeof(count), "%s", sp ? sp : "");
        size_t nlen = sp ? (size_t)(sp - buf) : strlen(buf);
        while (nlen > 1) {
            nlen--;
            snprintf(buf, sizeof(buf), "%.*s..%s", (int)nlen, top, count);
            if (text_w(FONT_NAME, buf) <= avail)
                break;
        }
    }
    text_at(layer, FONT_NAME, ox + RIGHT, oy + LANE_BASE, buf, c, LV_TEXT_ALIGN_RIGHT);
}

static void draw_korg(lv_layer_t *layer, int32_t ox, int32_t oy, const korg_view_t *k) {
    const bool st = k->stale;
    const lv_color_t accent = st || !k->have ? WS_COLOR_MUTED : WS_COLOR_LAVENDER;
    const lv_color_t fg = st ? WS_COLOR_MUTED : WS_COLOR_FG;
    const lv_color_t sub = st ? WS_COLOR_MUTED : KK_SUBTEXT0;

    text_at(layer, FONT_TITLE, ox + LEFT, oy + KORG_BASE, "korg", accent, LV_TEXT_ALIGN_LEFT);
    if (k->note[0])
        text_at(layer, FONT_NOTE, ox + RIGHT, oy + KORG_BASE, k->note,
                k->have ? WS_COLOR_YELLOW : WS_COLOR_MUTED, LV_TEXT_ALIGN_RIGHT);
    if (!k->have)
        return;

    char n[16];
    lv_snprintf(n, sizeof(n), "%d", k->open);
    int32_t x = ox + LEFT;
    x += text_at(layer, FONT_BIG, x, oy + OPEN_BASE, n, fg, LV_TEXT_ALIGN_LEFT) + 16;
    x += text_at(layer, FONT_OPEN, x, oy + OPEN_BASE, "open", sub, LV_TEXT_ALIGN_LEFT);
    int32_t sx = ox + SPARK_X;
    if (sx < x + 24)
        sx = x + 24;
    draw_spark(layer, ox, oy, sx, k, accent);

    /* The lanes: Awaiting Ken (peach when it is Ken's move), then active. */
    x = ox + LEFT;
    lv_snprintf(n, sizeof(n), "%d", k->awaiting);
    x += text_at(layer, FONT_LANE, x, oy + LANE_BASE, n,
                 k->awaiting > 0 && !st ? WS_COLOR_ORANGE : fg, LV_TEXT_ALIGN_LEFT) + 10;
    x += text_at(layer, FONT_TEXT, x, oy + LANE_BASE, "awaiting Ken", sub, LV_TEXT_ALIGN_LEFT) + 34;
    lv_snprintf(n, sizeof(n), "%d", k->active);
    x += text_at(layer, FONT_LANE, x, oy + LANE_BASE, n, fg, LV_TEXT_ALIGN_LEFT) + 10;
    x += text_at(layer, FONT_TEXT, x, oy + LANE_BASE, "active", sub, LV_TEXT_ALIGN_LEFT);

    if (k->top[0])
        draw_top(layer, ox, oy, x + 24, k->top, accent);
    draw_projbar(layer, ox, oy, k);
}

/* ---- klams ---- */

static lv_color_t health_color(kdash_klams_health_t h) {
    return h == KDASH_KLAMS_OK ? WS_COLOR_GREEN
           : h == KDASH_KLAMS_DEGRADED ? WS_COLOR_YELLOW
                                       : WS_COLOR_RED;
}

static void draw_scanner(lv_layer_t *layer, int32_t ox, int32_t oy, const klams_view_t *m, int i) {
    const bool st = m->stale;
    const int32_t base = oy + SCAN_BASE + i * SCAN_DY;
    const int32_t bx = ox + SCAN_BAR_X, by = base - SCAN_BAR_H;
    const kk_scan_state_t s = m->scan_state[i];

    text_at(layer, FONT_NAME, ox + LEFT, base, m->scan_name[i], st ? WS_COLOR_MUTED : WS_COLOR_FG,
            LV_TEXT_ALIGN_LEFT);
    if (s == KK_SCAN_STALLED && !st) {
        /* Stalled: an empty bar outlined in red, as the mock-up's alarm row. */
        rect(layer, bx, by, SCAN_BAR_W, SCAN_BAR_H, WS_COLOR_RED, SCAN_BAR_H / 2);
        rect(layer, bx + 3, by + 3, SCAN_BAR_W - 6, SCAN_BAR_H - 6, WS_COLOR_BG,
             (SCAN_BAR_H - 6) / 2);
    } else {
        rect(layer, bx, by, SCAN_BAR_W, SCAN_BAR_H, WS_COLOR_SURFACE0, SCAN_BAR_H / 2);
        if (m->scan_px[i] > 0) {
            /* On schedule: sapphire. No run time to judge by: the same hue
             * pulled halfway to the card, so it reads as a reading and never
             * as a verdict. */
            lv_color_t c = st                     ? WS_COLOR_MUTED
                           : s == KK_SCAN_NEUTRAL ? mixc(WS_COLOR_BG, WS_COLOR_SAPPHIRE, 0.45)
                                                  : WS_COLOR_SAPPHIRE;
            int32_t w = m->scan_px[i] < SCAN_BAR_H ? SCAN_BAR_H : m->scan_px[i];
            rect(layer, bx, by, w, SCAN_BAR_H, c, SCAN_BAR_H / 2);
        }
    }
    const bool alarm = s == KK_SCAN_STALLED && !st;
    text_at(layer, alarm ? FONT_TEXT_B : FONT_TEXT, ox + RIGHT, base, m->scan_text[i],
            st ? WS_COLOR_MUTED : alarm ? WS_COLOR_RED : KK_SUBTEXT0, LV_TEXT_ALIGN_RIGHT);
}

static void draw_subsystems(lv_layer_t *layer, int32_t ox, int32_t oy, const klams_view_t *m) {
    if (m->nsub <= 0)
        return;
    const bool st = m->stale;
    int32_t tw[KDASH_KLAMS_SUBSYSTEMS_MAX], tot = 0;
    for (int i = 0; i < m->nsub; i++) {
        const lv_font_t *f = m->sub_down[i] && !st ? FONT_TEXT_B : FONT_TEXT;
        tw[i] = 30 + text_w(f, m->sub_name[i]);
        tot += tw[i];
    }
    int32_t gap = m->nsub > 1 ? (INNER_W - tot) / (m->nsub - 1) : 0;
    if (gap < 12)
        gap = 12;
    int32_t px = ox + LEFT;
    const int32_t base = oy + SUBS_BASE;
    for (int i = 0; i < m->nsub; i++) {
        const bool down = m->sub_down[i] && !st;
        int32_t cx = px + 10;
        dot(layer, cx, base - 8, 10,
            st ? WS_COLOR_SURFACE1 : down ? WS_COLOR_RED : WS_COLOR_GREEN);
        text_at(layer, down ? FONT_TEXT_B : FONT_TEXT, cx + 20, base, m->sub_name[i],
                st ? WS_COLOR_MUTED : down ? WS_COLOR_RED : KK_SUBTEXT0, LV_TEXT_ALIGN_LEFT);
        px += tw[i] + gap;
    }
}

static void draw_klams(lv_layer_t *layer, int32_t ox, int32_t oy, const klams_view_t *m) {
    const bool st = m->stale;
    int32_t tw = text_at(layer, FONT_TITLE, ox + LEFT, oy + KLAMS_BASE, "klams",
                         st || !m->have ? WS_COLOR_MUTED : WS_COLOR_SAPPHIRE, LV_TEXT_ALIGN_LEFT);
    const int32_t after = ox + LEFT + tw + 24;
    if (!m->have) {
        text_at(layer, FONT_NOTE, ox + RIGHT, oy + KLAMS_BASE, m->note, WS_COLOR_MUTED,
                LV_TEXT_ALIGN_RIGHT);
        return;
    }
    /* Stale (the publisher is down): the note takes the health dot's place,
     * since a dot from an old record would claim to know. */
    if (st)
        text_at(layer, FONT_NOTE, after - 6, oy + KLAMS_BASE, m->note, WS_COLOR_YELLOW,
                LV_TEXT_ALIGN_LEFT);
    else
        dot(layer, after + 4, oy + KLAMS_BASE - 10, 10, health_color(m->health));

    if (!st && m->health == KDASH_KLAMS_DOWN) {
        /* Fresh and down: klams itself is down. The record's values are null;
         * the half says so rather than drawing blanks that look like zero. */
        text_at(layer, FONT_NAME, ox + RIGHT, oy + KLAMS_BASE, "down", WS_COLOR_RED,
                LV_TEXT_ALIGN_RIGHT);
        return;
    }
    if (m->chunks[0])
        text_at(layer, FONT_CHUNKS, ox + RIGHT, oy + KLAMS_BASE, m->chunks,
                st ? WS_COLOR_MUTED : WS_COLOR_FG, LV_TEXT_ALIGN_RIGHT);
    for (int i = 0; i < m->nscan; i++)
        draw_scanner(layer, ox, oy, m, i);
    draw_subsystems(layer, ox, oy, m);
}

/* ---- The card ---- */

static void draw_card(lv_layer_t *layer, const lv_area_t *a, const view_t *v) {
    /* The mock-up's cell origin: the object is CELL_PAD inside it. */
    const int32_t ox = a->x1 - CELL_PAD, oy = a->y1 - CELL_PAD;
    rect(layer, a->x1, a->y1, KK_CARD_W, KK_CARD_H, KK_CRUST, 0);
    rect(layer, ox + 7, oy + 7, 618, 614, WS_COLOR_BG, 18);
    draw_korg(layer, ox, oy, &v->k);
    rect(layer, ox + LEFT, oy + DIVIDER_Y, INNER_W, 2, WS_COLOR_SURFACE0, 0);
    draw_klams(layer, ox, oy, &v->m);
}

static void draw_cb(lv_event_t *e) {
    lv_obj_t *obj = lv_event_get_target(e);
    kk_widget_t *w = lv_obj_get_user_data(obj);
    if (!w || !w->cur.valid)
        return;
    lv_area_t a;
    lv_obj_get_coords(obj, &a);
    draw_card(lv_event_get_layer(e), &a, &w->cur);
}

static void delete_cb(lv_event_t *e) {
    free(lv_obj_get_user_data(lv_event_get_target(e)));
}

/* ---- Views ---- */

static void korg_view(korg_view_t *k, double now) {
    const kdash_korg_pulse_t *p = kk_korg();
    if (!p) {
        snprintf(k->note, sizeof(k->note), "no feed");
        return;
    }
    k->have = true;
    k->stale = kk_korg_stale(now);
    if (k->stale)
        kk_fmt_nodata(p->ts, now, k->note, sizeof(k->note));
    k->open = p->open;
    k->awaiting = p->awaiting;
    k->active = p->active;
    k->npts = kk_korg_spark(p, k->pts, KDASH_KORG_HISTORY_MAX);
    if (p->by_project_count > 0)
        snprintf(k->top, sizeof(k->top), "%.*s %d", KK_NAME_MAX - 1, p->by_project[0].name,
                 p->by_project[0].open);
    for (int i = 0; i < p->by_project_count; i++)
        k->seg[k->nseg++] = p->by_project[i].open;
    if (p->other_open > 0) {
        k->seg[k->nseg++] = p->other_open;
        k->seg_other = true;
    }
}

static void klams_view(klams_view_t *m, double now) {
    const kdash_klams_status_t *s = kk_klams();
    if (!s) {
        snprintf(m->note, sizeof(m->note), "no feed");
        return;
    }
    m->have = true;
    m->stale = kk_klams_stale(now);
    if (m->stale)
        kk_fmt_nodata(s->ts, now, m->note, sizeof(m->note));
    m->health = kk_klams_health();
    if (s->knowledge >= 0) {
        char n[24];
        kk_fmt_count(s->knowledge, n, sizeof(n));
        snprintf(m->chunks, sizeof(m->chunks), "%s chunks", n);
    }
    m->nscan = s->scanners_count < KK_SCANNER_ROWS ? s->scanners_count : KK_SCANNER_ROWS;
    for (int i = 0; i < m->nscan; i++) {
        kk_scan_view_t sv;
        kk_scan_view(&s->scanners[i], now, &sv);
        memcpy(m->scan_name[i], s->scanners[i].name, sizeof(m->scan_name[i]));
        m->scan_state[i] = sv.state;
        m->scan_px[i] = (int)lround(sv.frac * SCAN_BAR_W);
        memcpy(m->scan_text[i], sv.text, sizeof(m->scan_text[i]));
    }
    m->nsub = s->subsystems_count;
    for (int i = 0; i < m->nsub; i++) {
        memcpy(m->sub_name[i], s->subsystems[i].name, sizeof(m->sub_name[i]));
        m->sub_down[i] = kk_subsystem_down(i);
    }
}

/* ---- Public API ---- */

lv_obj_t *korg_klams_card_create(lv_obj_t *parent) {
    lv_obj_t *o = lv_obj_create(parent);
    lv_obj_remove_style_all(o);
    lv_obj_clear_flag(o, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_size(o, KK_CARD_W, KK_CARD_H);
    kk_widget_t *w = calloc(1, sizeof(*w));
    lv_obj_set_user_data(o, w);
    lv_obj_add_event_cb(o, delete_cb, LV_EVENT_DELETE, NULL);
    lv_obj_add_event_cb(o, draw_cb, LV_EVENT_DRAW_MAIN, NULL);
    return o;
}

void korg_klams_card_update(lv_obj_t *card, double now) {
    kk_widget_t *w = card ? lv_obj_get_user_data(card) : NULL;
    if (!w)
        return;
    view_t *v = &w->next;
    memset(v, 0, sizeof(*v));
    korg_view(&v->k, now);
    klams_view(&v->m, now);
    v->valid = true;

    const bool first = !w->cur.valid;
    const bool k_changed = first || memcmp(&v->k, &w->cur.k, sizeof(v->k)) != 0;
    const bool m_changed = first || memcmp(&v->m, &w->cur.m, sizeof(v->m)) != 0;
    w->cur = *v;
    if (k_changed && m_changed) {
        lv_obj_invalidate(card);
        return;
    }
    if (!k_changed && !m_changed)
        return;
    /* One half: the scanner bars drain every few seconds, and that must not
     * repaint korg's line; a korg write every 5 min must not repaint klams. */
    lv_area_t c;
    lv_obj_get_coords(card, &c);
    const int32_t split = c.y1 + HALF_Y - CELL_PAD;
    lv_area_t s = k_changed ? (lv_area_t){c.x1, c.y1, c.x2, split - 1}
                            : (lv_area_t){c.x1, split, c.x2, c.y2};
    lv_obj_invalidate_area(card, &s);
}
