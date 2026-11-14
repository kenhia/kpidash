/**
 * limits_card.c — The Claude limits card (WI #4609). See the header.
 *
 * Coordinates are the mock-up's (sprints/planning/row2-cards-mockups.html,
 * `limits()`), in its 632 × 628 cell, at real panel pixels. The object sits
 * CELL_PAD inside that cell, so every draw call offsets by the cell's origin.
 * Text y values are the SVG's baselines, which text_at() converts to LVGL's
 * top-of-line.
 *
 * Sprint 039 (WI #4631) adds the layouts for a record with no five-hour window,
 * which no mock-up draws: the same rings, colours and type, with the bar and its
 * rule gone and the rings moved down NOFIVE_DY so the group sits centred
 * between the header and the caption. Nothing new fills the freed height.
 */
#include "limits_card.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "common.h"
#include "layout.h"
#include "limstate.h"

/* Mocha colours the palette does not carry yet. */
#define LIM_CRUST lv_color_hex(0x11111B)
#define LIM_SUBTEXT0 lv_color_hex(0xA6ADC8)

#define FONT_BIG (&lv_font_montserrat_bold_96_digits)
#define FONT_PCT (&lv_font_montserrat_bold_36)
#define FONT_TITLE (&lv_font_montserrat_bold_28)
#define FONT_STALE (&lv_font_montserrat_bold_24)
#define FONT_LABEL_B (&lv_font_montserrat_bold_24)
#define FONT_TEXT (&lv_font_montserrat_24)
#define FONT_SMALL (&lv_font_montserrat_20)
#define FONT_SMALL_B (&lv_font_montserrat_bold_20)

/* ---- Geometry (mock-up `limits`) ---- */
#define RING_CX0 172 /* first ring's centre; the second is RING_DX right */
#define RING_DX 288
#define RING_CY 228
#define RING_R 106 /* stroke centre line */
#define RING_SW 26
#define TICK_IN 22 /* the pace tick spans r - 22 .. r + 22 */
#define BAR_X 36
#define BAR_W 560
#define BAR_Y 540
#define BAR_H 20
#define HDR_RIGHT 596
#define HDR_BASE 62
#define CARD_CX 316 /* the card's centre, cell-relative: 7 + 618 / 2 */
/* No five-hour window: the ring group (ring top 109 to the reset line's 441)
 * centred in the 66..585 between the header and the caption. */
#define NOFIVE_DY 50
#define LABEL_MAX_W (RING_DX - 16) /* a ring label may not reach its neighbour's */

#define PACE_STEPS 720 /* half a degree of ring: the tick's resolution */

typedef struct {
    int pct;
    int pace; /* 0..PACE_STEPS, -1 = no tick */
    char reset[32];
} gauge_t;

/* Everything that, when it changes, repaints the whole card. */
typedef struct {
    lim_mode_t mode;
    gauge_t g[2];
    char label[2][48];
    bool g_stale[2]; /* per ring: the headline's stamp, or the scoped set's */
    bool h_stale;
    bool scoped;
    gauge_t s;
    char s_label[48];
    bool s_stale;
} body_t;

typedef struct {
    body_t body;
    char header[96];
    bool valid;
} view_t;

typedef struct {
    view_t cur;
    view_t next; /* scratch, zeroed each update so memcmp sees only content */
} lim_widget_t;

static lv_color_t band_color(int pct, bool stale) {
    if (stale)
        return WS_COLOR_MUTED;
    switch (lim_band(pct)) {
    case LIM_BAND_CRIT:
        return WS_COLOR_RED;
    case LIM_BAND_WARN:
        return WS_COLOR_YELLOW;
    default:
        return WS_COLOR_ORANGE; /* peach: Ken's ruling, 2026-11-13 */
    }
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

static void arc(lv_layer_t *layer, int32_t cx, int32_t cy, double start, double end,
                lv_color_t c, bool rounded) {
    lv_draw_arc_dsc_t d;
    lv_draw_arc_dsc_init(&d);
    d.color = c;
    d.opa = LV_OPA_COVER;
    d.width = RING_SW;
    d.center.x = cx;
    d.center.y = cy;
    d.radius = RING_R + RING_SW / 2; /* LVGL's radius is the outer edge */
    d.start_angle = (lv_value_precise_t)start;
    d.end_angle = (lv_value_precise_t)end;
    d.rounded = rounded;
    lv_draw_arc(layer, &d);
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

/* The regular 20 px font has no "·" (LVGL's built-in Montserrat stops at
 * ASCII), so a regular-20 line draws it in bold 20, which has it — the same
 * runs trick the weather card uses. Other fonts draw whole. */
#define DOT "\xC2\xB7"

static int32_t run_w(const char *s, uint32_t len, const lv_font_t *f, int32_t ls) {
    return lv_text_get_width(s, len, f, ls);
}

static int32_t text_w(const lv_font_t *font, const char *s, int32_t ls) {
    if (font != FONT_SMALL)
        return run_w(s, (uint32_t)strlen(s), font, ls);
    int32_t w = 0;
    const char *p = s, *d;
    while ((d = strstr(p, DOT)) != NULL) {
        w += run_w(p, (uint32_t)(d - p), font, ls) + run_w(DOT, 2, FONT_SMALL_B, ls);
        p = d + 2;
    }
    return w + run_w(p, (uint32_t)strlen(p), font, ls);
}

static int32_t draw_run(lv_layer_t *layer, const lv_font_t *f, int32_t x, int32_t baseline,
                        const char *s, uint32_t len, lv_color_t c, int32_t ls) {
    if (len == 0)
        return 0;
    char buf[96];
    if (len > sizeof(buf) - 1)
        len = sizeof(buf) - 1;
    memcpy(buf, s, len);
    buf[len] = '\0';
    int32_t w = run_w(buf, len, f, ls);
    lv_draw_label_dsc_t d;
    lv_draw_label_dsc_init(&d);
    d.text = buf;
    d.text_local = 1;
    d.font = f;
    d.color = c;
    d.opa = LV_OPA_COVER;
    d.letter_space = ls;
    int32_t top = baseline - (f->line_height - f->base_line);
    lv_area_t a = {x, top, x + w, top + f->line_height - 1};
    lv_draw_label(layer, &d, &a);
    return w;
}

/* Text whose BASELINE is at y. `x` is the left edge, the centre or the right
 * edge by `align`; `ls` is letter spacing. */
static void text_at(lv_layer_t *layer, const lv_font_t *font, int32_t x, int32_t baseline,
                    const char *s, lv_color_t c, lv_text_align_t align, int32_t ls) {
    if (!s || !s[0])
        return;
    int32_t w = text_w(font, s, ls);
    if (align == LV_TEXT_ALIGN_CENTER)
        x -= w / 2;
    else if (align == LV_TEXT_ALIGN_RIGHT)
        x -= w;
    if (font != FONT_SMALL) {
        draw_run(layer, font, x, baseline, s, (uint32_t)strlen(s), c, ls);
        return;
    }
    const char *p = s, *d;
    while ((d = strstr(p, DOT)) != NULL) {
        x += draw_run(layer, font, x, baseline, p, (uint32_t)(d - p), c, ls);
        x += draw_run(layer, FONT_SMALL_B, x, baseline, DOT, 2, c, ls);
        p = d + 2;
    }
    draw_run(layer, font, x, baseline, p, (uint32_t)strlen(p), c, ls);
}

/* ---- The card ---- */

static void draw_ring(lv_layer_t *layer, int32_t cx, int32_t oy, const gauge_t *g,
                      const char *label, bool stale) {
    const int32_t cy = oy + RING_CY;
    const lv_color_t fg = stale ? WS_COLOR_MUTED : WS_COLOR_FG;
    const lv_color_t sub = stale ? WS_COLOR_MUTED : LIM_SUBTEXT0;

    /* Track, then the value from twelve o'clock clockwise (LVGL's 0° is
     * three o'clock, so twelve is 270°). */
    arc(layer, cx, cy, 0, 360, WS_COLOR_SURFACE0, false);
    if (g->pct > 0) {
        double sweep = 360.0 * (g->pct >= 100 ? 0.99999 : g->pct / 100.0);
        arc(layer, cx, cy, 270, 270 + sweep, band_color(g->pct, stale), true);
    }
    if (g->pace >= 0) {
        double a = 2 * M_PI * g->pace / PACE_STEPS, s = sin(a), c = -cos(a);
        line(layer, cx + (int32_t)lround((RING_R - TICK_IN) * s),
             cy + (int32_t)lround((RING_R - TICK_IN) * c),
             cx + (int32_t)lround((RING_R + TICK_IN) * s),
             cy + (int32_t)lround((RING_R + TICK_IN) * c),
             stale ? WS_COLOR_SURFACE1 : WS_COLOR_FG, 6);
    }

    /* The number and a smaller "%" (the 96 px font is digits only), centred
     * as one run. */
    char num[8];
    lv_snprintf(num, sizeof(num), "%d", g->pct);
    int32_t nw = text_w(FONT_BIG, num, 0), pw = text_w(FONT_PCT, "%", 0);
    int32_t x = cx - (nw + 4 + pw) / 2;
    text_at(layer, FONT_BIG, x, cy + 34, num, fg, LV_TEXT_ALIGN_LEFT, 0);
    text_at(layer, FONT_PCT, x + nw + 4, cy + 34, "%", sub, LV_TEXT_ALIGN_LEFT, 0);

    text_at(layer, FONT_LABEL_B, cx, oy + 400, label, sub, LV_TEXT_ALIGN_CENTER, 2);
    text_at(layer, FONT_TEXT, cx, oy + 436, g->reset, fg, LV_TEXT_ALIGN_CENTER, 0);
}

static void draw_header(lv_layer_t *layer, int32_t ox, int32_t oy, const view_t *v) {
    if (v->body.h_stale)
        text_at(layer, FONT_STALE, ox + HDR_RIGHT, oy + HDR_BASE, v->header, WS_COLOR_YELLOW,
                LV_TEXT_ALIGN_RIGHT, 0);
    else
        text_at(layer, FONT_SMALL, ox + HDR_RIGHT, oy + HDR_BASE, v->header, WS_COLOR_MUTED,
                LV_TEXT_ALIGN_RIGHT, 0);
}

static void draw_card(lv_layer_t *layer, const lv_area_t *a, const view_t *v) {
    const body_t *b = &v->body;
    /* The mock-up's cell origin: the object is CELL_PAD inside it. */
    const int32_t ox = a->x1 - CELL_PAD, oy = a->y1 - CELL_PAD;

    /* The card's fill sits on the panel's crust, as in the mock-up's shell. */
    rect(layer, a->x1, a->y1, LIM_CARD_W, LIM_CARD_H, LIM_CRUST, 0);
    rect(layer, ox + 7, oy + 7, 618, 614, WS_COLOR_BG, 18);

    text_at(layer, FONT_TITLE, ox + 36, oy + HDR_BASE, "Claude",
            b->h_stale ? WS_COLOR_MUTED : WS_COLOR_ORANGE, LV_TEXT_ALIGN_LEFT, 0);
    draw_header(layer, ox, oy, v);

    if (b->mode == LIM_MODE_SEVEN) {
        draw_ring(layer, ox + CARD_CX, oy + NOFIVE_DY, &b->g[0], b->label[0], b->g_stale[0]);
    } else {
        const int32_t dy = b->mode == LIM_MODE_SCOPED ? NOFIVE_DY : 0;
        for (int i = 0; i < 2; i++)
            draw_ring(layer, ox + RING_CX0 + i * RING_DX, oy + dy, &b->g[i], b->label[i],
                      b->g_stale[i]);
    }

    /* The model-scoped window as a bar, in the five-hour layout only. With no
     * scoped set the bar area is left out rather than drawn empty. */
    if (b->mode == LIM_MODE_FIVE && b->scoped) {
        const bool st = b->s_stale;
        const lv_color_t fg = st ? WS_COLOR_MUTED : WS_COLOR_FG;
        rect(layer, ox + BAR_X, oy + 466, BAR_W, 2, WS_COLOR_SURFACE0, 0);
        text_at(layer, FONT_TEXT, ox + BAR_X, oy + 516, b->s_label,
                st ? WS_COLOR_MUTED : LIM_SUBTEXT0, LV_TEXT_ALIGN_LEFT, 0);
        char pct[8];
        lv_snprintf(pct, sizeof(pct), "%d%%", b->s.pct);
        text_at(layer, FONT_PCT, ox + HDR_RIGHT, oy + 518, pct, fg, LV_TEXT_ALIGN_RIGHT, 0);
        rect(layer, ox + BAR_X, oy + BAR_Y, BAR_W, BAR_H, WS_COLOR_SURFACE0, BAR_H / 2);
        int32_t fw = (int32_t)lround(BAR_W * b->s.pct / 100.0);
        rect(layer, ox + BAR_X, oy + BAR_Y, fw < BAR_H ? BAR_H : fw, BAR_H,
             band_color(b->s.pct, st), BAR_H / 2);
        if (b->s.pace >= 0) {
            int32_t tx = BAR_X + (int32_t)lround((double)BAR_W * b->s.pace / PACE_STEPS);
            rect(layer, ox + tx - 3, oy + 530, 6, 40, st ? WS_COLOR_SURFACE1 : WS_COLOR_FG, 3);
        }
    }
    text_at(layer, FONT_SMALL, ox + BAR_X, oy + 600, "white tick: how far through the window",
            WS_COLOR_MUTED, LV_TEXT_ALIGN_LEFT, 0);
}

static void draw_cb(lv_event_t *e) {
    lv_obj_t *obj = lv_event_get_target(e);
    lim_widget_t *lw = lv_obj_get_user_data(obj);
    if (!lw || !lw->cur.valid)
        return;
    lv_area_t a;
    lv_obj_get_coords(obj, &a);
    draw_card(lv_event_get_layer(e), &a, &lw->cur);
}

static void delete_cb(lv_event_t *e) {
    free(lv_obj_get_user_data(lv_event_get_target(e)));
}

static int pace_steps(double resets_at, double window_s, double now) {
    double p = lim_pace(resets_at, window_s, now);
    return p < 0 ? -1 : (int)lround(p * PACE_STEPS);
}

static void gauge(gauge_t *g, double pct, double resets_at, double window_s, double now) {
    g->pct = lim_pct(pct);
    g->pace = pace_steps(resets_at, window_s, now);
    lim_fmt_reset(resets_at, now, g->reset, sizeof(g->reset));
}

/* The scoped ring's label: "FABLE · 7 DAY" when it fits under the ring, else
 * the model alone, else the model clipped with an ellipsis. */
static void scoped_ring_label(const lim_record_t *r, char *buf, size_t n) {
    lim_fmt_scoped_ring_label(r, true, buf, n);
    if (text_w(FONT_LABEL_B, buf, 2) <= LABEL_MAX_W)
        return;
    lim_fmt_scoped_ring_label(r, false, buf, n);
    size_t len = strlen(buf);
    if (text_w(FONT_LABEL_B, buf, 2) <= LABEL_MAX_W || n < 4)
        return;
    while (len > 0) {
        len--;
        while (len > 0 && ((unsigned char)buf[len] & 0xC0) == 0x80)
            len--; /* never split a UTF-8 sequence */
        if (len + 4 > n)
            continue;
        memcpy(buf + len, "\xE2\x80\xA6", 4); /* "…" */
        if (text_w(FONT_LABEL_B, buf, 2) <= LABEL_MAX_W)
            return;
    }
}

/* ---- Public API ---- */

lv_obj_t *limits_card_create(lv_obj_t *parent) {
    lv_obj_t *o = lv_obj_create(parent);
    lv_obj_remove_style_all(o);
    lv_obj_clear_flag(o, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_size(o, LIM_CARD_W, LIM_CARD_H);
    lim_widget_t *lw = calloc(1, sizeof(*lw));
    lv_obj_set_user_data(o, lw);
    lv_obj_add_event_cb(o, delete_cb, LV_EVENT_DELETE, NULL);
    lv_obj_add_event_cb(o, draw_cb, LV_EVENT_DRAW_MAIN, NULL);
    return o;
}

void limits_card_update(lv_obj_t *card, double now) {
    lim_widget_t *lw = card ? lv_obj_get_user_data(card) : NULL;
    const lim_record_t *r = lim_record();
    if (!lw || !r)
        return;

    view_t *v = &lw->next;
    memset(v, 0, sizeof(*v));
    body_t *b = &v->body;
    const kdash_claude_limits_t *l = &r->l;
    b->mode = lim_mode();
    b->h_stale = lim_headline_stale(r, now);
    if (b->mode == LIM_MODE_FIVE) {
        gauge(&b->g[0], l->five_hour_pct, l->five_hour_resets_at, LIM_FIVE_HOUR_S, now);
        gauge(&b->g[1], l->seven_day_pct, l->seven_day_resets_at, LIM_SEVEN_DAY_S, now);
        lv_snprintf(b->label[0], sizeof(b->label[0]), "5 HOUR");
        lv_snprintf(b->label[1], sizeof(b->label[1]), "7 DAY");
        b->g_stale[0] = b->g_stale[1] = b->h_stale;
    } else {
        gauge(&b->g[0], l->seven_day_pct, l->seven_day_resets_at, LIM_SEVEN_DAY_S, now);
        lv_snprintf(b->label[0], sizeof(b->label[0]), "7 DAY");
        b->g_stale[0] = b->h_stale;
        /* SCOPED is only ever chosen with a scoped set, and the set is
         * carried once seen; the check guards the debounce's lag anyway. */
        if (b->mode == LIM_MODE_SCOPED && l->scoped_valid) {
            gauge(&b->g[1], l->scoped_pct, l->scoped_resets_at, LIM_SEVEN_DAY_S, now);
            scoped_ring_label(r, b->label[1], sizeof(b->label[1]));
            b->g_stale[1] = lim_scoped_stale(r, now);
        } else {
            b->mode = LIM_MODE_SEVEN;
        }
    }
    if (b->mode == LIM_MODE_FIVE && l->scoped_valid) {
        b->scoped = true;
        gauge(&b->s, l->scoped_pct, l->scoped_resets_at, LIM_SEVEN_DAY_S, now);
        b->s.reset[0] = '\0'; /* the bar draws no reset text */
        lim_fmt_scoped_label(r, b->s_label, sizeof(b->s_label));
        b->s_stale = lim_scoped_stale(r, now);
    }
    lim_fmt_header(r, now, b->h_stale, v->header, sizeof(v->header));
    v->valid = true;

    if (!lw->cur.valid || memcmp(b, &lw->cur.body, sizeof(*b)) != 0) {
        lw->cur = *v;
        lv_obj_invalidate(card);
        return;
    }
    if (strcmp(v->header, lw->cur.header) != 0) {
        /* The header's right half, card-relative: the age ticks every second
         * and nothing else there moves. */
        lv_area_t c;
        lv_obj_get_coords(card, &c);
        lv_area_t s = {c.x1 + 200, c.y1 + 24, c.x1 + LIM_CARD_W - 20, c.y1 + 76};
        lv_obj_invalidate_area(card, &s);
    }
    lw->cur = *v;
}
