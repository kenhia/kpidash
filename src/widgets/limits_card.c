/**
 * limits_card.c — The Claude limits card (WI #4609). See the header.
 *
 * Coordinates are the mock-up's (sprints/planning/row2-cards-mockups.html,
 * `limits()`), in its 632 × 628 cell, at real panel pixels. The object sits
 * CELL_PAD inside that cell, so every draw call offsets by the cell's origin.
 * Text y values are the SVG's baselines, which text_at() converts to LVGL's
 * top-of-line.
 *
 * Sprint 049 (WI #4941, korg:4950) makes it the "Usage" card: Ken's layout C,
 * three peer rings in a triangle — Claude 7 day and Claude's scoped (Fable)
 * window on top, Codex 7 day centred below in the Codex green — and long
 * windows only (kctrldeck carries the 5-hour bars). The rings shrank from
 * r 106 to r 80 to fit two rows, so the number moved to a 72 px digits face.
 * Every slot is fixed: a ring whose feed has never published is simply not
 * drawn, and nothing moves when it appears. The five-hour layout, its bar and
 * sprint 039's no-five-hour layouts are gone (sprint 049's record says why).
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
/* The Codex green: kctrldeck's --accent-codex (branch 017-the-usage-column),
 * approved by Ken on the panel (korg:4950 comment 5120). */
#define LIM_CODEX_GREEN lv_color_hex(0x1D9E75)

#define FONT_BIG (&lv_font_montserrat_bold_72_digits)
#define FONT_PCT (&lv_font_montserrat_bold_28)
#define FONT_TITLE (&lv_font_montserrat_bold_28)
#define FONT_STALE (&lv_font_montserrat_bold_24)
#define FONT_LABEL_B (&lv_font_montserrat_bold_24)
#define FONT_TEXT (&lv_font_montserrat_24)
#define FONT_SMALL (&lv_font_montserrat_20)
#define FONT_SMALL_B (&lv_font_montserrat_bold_20)

/* ---- Geometry (cell-relative; the card is 618 x 614 at 7,7) ---- */
#define RING_R 80 /* stroke centre line */
#define RING_SW 22
#define TICK_IN 18 /* the pace tick spans r - 18 .. r + 18 */
#define TOP_CX0 172 /* the top rings' centres: 172 and 172 + TOP_DX */
#define TOP_DX 288
#define TOP_CY 176
#define BOT_CX 316 /* the card's centre */
/* The bottom ring overlaps the top rings' reset lines horizontally, so it
 * sits just clear of them: its outer edge (r + sw / 2 = 91) is at 323. */
#define BOT_CY 414
#define NUM_DY 26    /* the number's baseline below the centre */
#define LABEL_DY 112 /* the label's baseline below the centre */
#define RESET_DY 142 /* the reset line's */
#define HDR_RIGHT 596
#define HDR_BASE 62
#define LABEL_MAX_W (TOP_DX - 16) /* a ring label may not reach its neighbour's */

#define PACE_STEPS 720 /* the tick's resolution: 0.375° of the 270° arc */

enum { R_CLAUDE = 0, R_SCOPED, R_CODEX, R_COUNT };

typedef struct {
    bool shown;
    bool codex; /* the Codex green in the normal band */
    bool stale;
    int pct;
    int pace; /* 0..PACE_STEPS, -1 = no tick */
    char reset[32];
    char label[48];
} ring_t;

/* Everything that, when it changes, repaints the whole card. */
typedef struct {
    ring_t r[R_COUNT];
    bool h_stale;   /* the header reads as a warning */
    bool all_stale; /* every ring drawn is stale: the title greys too */
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

/* The value arc's colour. Codex keeps its identity green in the normal band
 * and shifts to the same yellow and red as the Claude rings, at the same
 * thresholds (the overseer's reading of Ken's words, korg:4950 comment 5120). */
static lv_color_t band_color(int pct, bool stale, bool codex) {
    if (stale)
        return WS_COLOR_MUTED;
    switch (lim_band(pct)) {
    case LIM_BAND_CRIT:
        return WS_COLOR_RED;
    case LIM_BAND_WARN:
        return WS_COLOR_YELLOW;
    default:
        return codex ? LIM_CODEX_GREEN : WS_COLOR_ORANGE; /* peach: Ken, 2026-11-13 */
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

static void draw_ring(lv_layer_t *layer, int32_t cx, int32_t cy, const ring_t *g) {
    const lv_color_t fg = g->stale ? WS_COLOR_MUTED : WS_COLOR_FG;
    const lv_color_t sub = g->stale ? WS_COLOR_MUTED : LIM_SUBTEXT0;

    /* Track, then the value, as 270° arcs open at the bottom (WI #4801): a
     * ring with a visible start and end gives the pace tick something to be
     * read against. Both have rounded ends. */
    arc(layer, cx, cy, lim_gauge_deg(0), lim_gauge_deg(1), WS_COLOR_SURFACE0, true);
    if (g->pct > 0)
        arc(layer, cx, cy, lim_gauge_deg(0), lim_gauge_deg(g->pct / 100.0),
            band_color(g->pct, g->stale, g->codex), true);
    if (g->pace >= 0) {
        double a = lim_gauge_deg((double)g->pace / PACE_STEPS) * M_PI / 180;
        double c = cos(a), s = sin(a);
        line(layer, cx + (int32_t)lround((RING_R - TICK_IN) * c),
             cy + (int32_t)lround((RING_R - TICK_IN) * s),
             cx + (int32_t)lround((RING_R + TICK_IN) * c),
             cy + (int32_t)lround((RING_R + TICK_IN) * s),
             g->stale ? WS_COLOR_SURFACE1 : WS_COLOR_FG, 5);
    }

    /* The number and a smaller "%" (the big face is digits only), centred
     * as one run. */
    char num[8];
    lv_snprintf(num, sizeof(num), "%d", g->pct);
    int32_t nw = text_w(FONT_BIG, num, 0), pw = text_w(FONT_PCT, "%", 0);
    int32_t x = cx - (nw + 3 + pw) / 2;
    text_at(layer, FONT_BIG, x, cy + NUM_DY, num, fg, LV_TEXT_ALIGN_LEFT, 0);
    text_at(layer, FONT_PCT, x + nw + 3, cy + NUM_DY, "%", sub, LV_TEXT_ALIGN_LEFT, 0);

    text_at(layer, FONT_LABEL_B, cx, cy + LABEL_DY, g->label, sub, LV_TEXT_ALIGN_CENTER, 2);
    text_at(layer, FONT_TEXT, cx, cy + RESET_DY, g->reset, fg, LV_TEXT_ALIGN_CENTER, 0);
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
    /* The cell origin: the object is CELL_PAD inside it. */
    const int32_t ox = a->x1 - CELL_PAD, oy = a->y1 - CELL_PAD;

    /* The card's fill sits on the panel's crust, as in the mock-up's shell. */
    rect(layer, a->x1, a->y1, LIM_CARD_W, LIM_CARD_H, LIM_CRUST, 0);
    rect(layer, ox + 7, oy + 7, 618, 614, WS_COLOR_BG, 18);

    text_at(layer, FONT_TITLE, ox + 36, oy + HDR_BASE, "Usage",
            b->all_stale ? WS_COLOR_MUTED : WS_COLOR_ORANGE, LV_TEXT_ALIGN_LEFT, 0);
    draw_header(layer, ox, oy, v);

    static const int32_t CX[R_COUNT] = {TOP_CX0, TOP_CX0 + TOP_DX, BOT_CX};
    static const int32_t CY[R_COUNT] = {TOP_CY, TOP_CY, BOT_CY};
    for (int i = 0; i < R_COUNT; i++)
        if (b->r[i].shown)
            draw_ring(layer, ox + CX[i], oy + CY[i], &b->r[i]);

    text_at(layer, FONT_SMALL, ox + 36, oy + 600, "white tick: how far through the window",
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

static void gauge(ring_t *g, double pct, double resets_at, double window_s, double now) {
    g->shown = true;
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

/* The header's right-hand text. Claude's record leads, as it always has
 * ("kai · 40s ago", or "no data 22m" in yellow once its headline is stale).
 * A stale Codex record beside a fresh Claude one says so itself — "codex: no
 * data 22m" — so a ring greying is never the only sign. With only Codex ever
 * seen, Codex's own host and age. Returns whether the text is a warning. */
static bool header_text(const lim_record_t *r, const kdash_codex_limits_t *c, double now,
                        char *buf, size_t n) {
    bool r_stale = r && lim_headline_stale(r, now);
    bool c_stale = c && lim_codex_stale(c, now);
    if (r && (r_stale || !c_stale)) {
        lim_fmt_header(r, now, r_stale, buf, n);
        return r_stale;
    }
    if (!c)
        return false; /* unreachable: the caller has one record or the other */
    char d[24];
    lim_fmt_dur(now - c->updated_at, d, sizeof(d));
    if (c_stale) {
        lv_snprintf(buf, n, "codex: no data %s", d);
        return true;
    }
    lv_snprintf(buf, n, "%s \xC2\xB7 %s ago", c->host, d);
    return false;
}

void limits_card_update(lv_obj_t *card, double now) {
    lim_widget_t *lw = card ? lv_obj_get_user_data(card) : NULL;
    const lim_record_t *r = lim_record();
    const kdash_codex_limits_t *c = lim_codex();
    if (!lw || (!r && !c))
        return;

    view_t *v = &lw->next;
    memset(v, 0, sizeof(*v));
    body_t *b = &v->body;
    if (r) {
        const kdash_claude_limits_t *l = &r->l;
        ring_t *g = &b->r[R_CLAUDE];
        gauge(g, l->seven_day_pct, l->seven_day_resets_at, LIM_SEVEN_DAY_S, now);
        g->stale = lim_headline_stale(r, now);
        lv_snprintf(g->label, sizeof(g->label), "CLAUDE \xC2\xB7 7 DAY");
        /* The scoped (Fable) window paces on the seven-day clock, as the deck
         * does (it cites LIM_SEVEN_DAY_S). limstate carries the set once
         * seen, so this ring does not blink on a write that lacks it. */
        if (l->scoped_valid) {
            ring_t *s = &b->r[R_SCOPED];
            gauge(s, l->scoped_pct, l->scoped_resets_at, LIM_SEVEN_DAY_S, now);
            s->stale = lim_scoped_stale(r, now);
            scoped_ring_label(r, s->label, sizeof(s->label));
            if (lim_resets_agree(l->scoped_resets_at, l->seven_day_resets_at))
                s->reset[0] = '\0'; /* the Claude 7-day ring's line says it */
        }
    }
    if (c) {
        ring_t *g = &b->r[R_CODEX];
        gauge(g, c->seven_day_pct, c->seven_day_resets_at, LIM_SEVEN_DAY_S, now);
        g->codex = true;
        g->stale = lim_codex_stale(c, now);
        lv_snprintf(g->label, sizeof(g->label), "CODEX \xC2\xB7 7 DAY");
    }
    b->all_stale = true;
    for (int i = 0; i < R_COUNT; i++)
        if (b->r[i].shown && !b->r[i].stale)
            b->all_stale = false;
    b->h_stale = header_text(r, c, now, v->header, sizeof(v->header));
    v->valid = true;

    if (!lw->cur.valid || memcmp(b, &lw->cur.body, sizeof(*b)) != 0) {
        lw->cur = *v;
        lv_obj_invalidate(card);
        return;
    }
    if (strcmp(v->header, lw->cur.header) != 0) {
        /* The header's right half, card-relative: the age ticks every second
         * and nothing else there moves. */
        lv_area_t a;
        lv_obj_get_coords(card, &a);
        lv_area_t h = {a.x1 + 200, a.y1 + 24, a.x1 + LIM_CARD_W - 20, a.y1 + 76};
        lv_obj_invalidate_area(card, &h);
    }
    lw->cur = *v;
}
