/**
 * calendar_card.c — the time and calendar card (WI #4665). See the header.
 *
 * Coordinates are the mock-up's (sprints/planning/calendar-mockups.html,
 * `cardA`, `sideClock`, `grid`, `cell`, `chip`), in its 1264 × 628 frame, at
 * real panel pixels. The object sits CELL_PAD inside that frame, so every
 * draw call offsets by the frame's origin. Text y values are the SVG's
 * baselines, which text_at() converts to LVGL's top-of-line.
 *
 * Every fill is opaque and every tint pre-mixed (sprint 030): the mock-up's
 * mix() is lv_color_mix() here, done once per colour, never a blend.
 */
#include "calendar_card.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "calstate.h"
#include "common.h"
#include "layout.h"

/* Mocha colours the palette does not carry yet. */
#define CAL_CRUST lv_color_hex(0x11111B)
#define CAL_SUBTEXT0 lv_color_hex(0xA6ADC8)
#define CAL_ROSE lv_color_hex(0xEBA0AC) /* Maroon: a holiday, not a warning */

#define FONT_CLOCK (&lv_font_montserrat_bold_96_digits)
#define FONT_DAY (&lv_font_montserrat_bold_36)
#define FONT_DATE (&lv_font_montserrat_36)
#define FONT_HEAD (&lv_font_montserrat_bold_20)
#define FONT_LABEL (&lv_font_montserrat_bold_24)
#define FONT_SUB (&lv_font_montserrat_20)
#define FONT_SUB_B (&lv_font_montserrat_bold_20) /* the middle dot: not in the built-in face */
#define FONT_NOTE (&lv_font_montserrat_24)
#define FONT_TITLE (&lv_font_montserrat_bold_28)
#define FONT_NUM (&lv_font_montserrat_36)
#define FONT_NUM_B (&lv_font_montserrat_bold_36)
#define FONT_CHIP (&lv_font_montserrat_28)
#define FONT_CHIP_B (&lv_font_montserrat_bold_28)

/* ---- Geometry (mock-up `sideClock`, `cardA`, `grid`) ---- */
#define LEFT 36
#define CLOCK_BASE 138
#define DAY_BASE 200
#define DATE_BASE 246
#define RULE_Y 282
#define RULE_END 430
#define COMING_BASE 326
#define UP_Y0 346
#define UP_DY 62
#define UP_TEXT_X 116
#define UP_TEXT_MAX 466 /* a label's right limit: clear of the grid at 480 */
#define NOTE_BASE 376
#define CHIP_W 64
#define CHIP_H 48
#define GRID_X 480
#define GRID_Y 112
#define GRID_W 748
#define GRID_H 484
#define GRID_GAP 6
#define TITLE_BASE 62
#define CELL_R 10
#define RING_W 4
/* The clock's repaint rectangle, in the frame: the 96 px line and margin. */
#define CLOCK_X0 30
#define CLOCK_Y0 30
#define CLOCK_X1 470
#define CLOCK_Y1 160

typedef struct {
    cal_view_t cur;
    bool valid;
    cal_view_t next; /* scratch: cal_view zeroes it, so memcmp sees only content */
} cal_widget_t;

/* JS mix(a, b, t): `t` of a, the rest b. Opaque. */
static lv_color_t mix(lv_color_t a, lv_color_t b, double t) {
    return lv_color_mix(a, b, (uint8_t)lround(t * 255));
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

/* A filled rectangle with an opaque border inside its edge. */
static void ring_rect(lv_layer_t *layer, int32_t x, int32_t y, int32_t w, int32_t h,
                      lv_color_t fill, lv_color_t border, int32_t bw, int32_t radius) {
    lv_draw_rect_dsc_t d;
    lv_draw_rect_dsc_init(&d);
    d.bg_color = fill;
    d.bg_opa = LV_OPA_COVER;
    d.border_color = border;
    d.border_width = bw;
    d.border_opa = LV_OPA_COVER;
    d.radius = radius;
    lv_area_t a = {x, y, x + w - 1, y + h - 1};
    lv_draw_rect(layer, &d, &a);
}

static int32_t text_w(const lv_font_t *font, const char *s, int32_t ls) {
    return lv_text_get_width(s, (uint32_t)strlen(s), font, ls);
}

/* Text whose BASELINE is at y, `ls` px between letters. `x` is the left edge,
 * the centre for LV_TEXT_ALIGN_CENTER, or the right edge for
 * LV_TEXT_ALIGN_RIGHT. Returns the width drawn. */
static int32_t text_at(lv_layer_t *layer, const lv_font_t *font, int32_t x, int32_t baseline,
                       const char *s, lv_color_t c, lv_text_align_t align, int32_t ls) {
    if (!s || !s[0])
        return 0;
    int32_t w = text_w(font, s, ls);
    if (align == LV_TEXT_ALIGN_RIGHT)
        x -= w;
    else if (align == LV_TEXT_ALIGN_CENTER)
        x -= w / 2;
    lv_draw_label_dsc_t d;
    lv_draw_label_dsc_init(&d);
    d.text = s;
    d.text_local = 1; /* callers pass stack buffers; the draw task outlives them */
    d.font = font;
    d.color = c;
    d.opa = LV_OPA_COVER;
    d.letter_space = ls;
    int32_t top = baseline - (font->line_height - font->base_line);
    lv_area_t a = {x, top, x + w, top + font->line_height - 1};
    lv_draw_label(layer, &d, &a);
    return w;
}

/* `s` cut with an ellipsis until it fits `max` px, on a UTF-8 boundary. */
static void fit(const lv_font_t *font, const char *s, int32_t max, char *out, size_t n) {
    snprintf(out, n, "%s", s);
    if (text_w(font, out, 0) <= max)
        return;
    size_t len = strlen(out);
    while (len > 0) {
        do
            len--;
        while (len > 0 && ((unsigned char)out[len] & 0xC0) == 0x80);
        if (len + 4 > n)
            continue;
        memcpy(out + len, "\xE2\x80\xA6", 4); /* … and the NUL */
        if (text_w(font, out, 0) <= max)
            return;
    }
}

/* ---- One cell (mock-up `cell`, style 'fill') ---- */

static void draw_cell(lv_layer_t *layer, int32_t x, int32_t y, int32_t w, int32_t h, int mday,
                      uint8_t f, bool chip) {
    const lv_color_t base = WS_COLOR_BG;
    lv_color_t bg = mix(WS_COLOR_SURFACE0, base, 0.22);
    lv_color_t num = (f & CAL_PAST) ? WS_COLOR_MUTED : WS_COLOR_FG;
    if (f & CAL_WEEKEND)
        bg = mix(WS_COLOR_SURFACE0, base, 0.75);
    if (f & CAL_OFF)
        bg = mix(WS_COLOR_BLUE, base, 0.30);
    if (f & CAL_HOLIDAY)
        num = (f & CAL_PAST) ? mix(CAL_ROSE, base, 0.55) : CAL_ROSE;
    if (f & CAL_TODAY) {
        bg = WS_COLOR_BLUE;
        num = CAL_CRUST;
    }
    if (f & CAL_SPECIAL)
        /* The SVG strokes its 4 px ring on the edge, half outside. */
        ring_rect(layer, x - RING_W / 2, y - RING_W / 2, w + RING_W, h + RING_W, bg,
                  WS_COLOR_YELLOW, RING_W, CELL_R + RING_W / 2);
    else
        rect(layer, x, y, w, h, bg, CELL_R);

    const bool bold = f & (CAL_TODAY | CAL_OFF | CAL_HOLIDAY | CAL_SPECIAL);
    const lv_font_t *font = chip ? (bold ? FONT_CHIP_B : FONT_CHIP) : (bold ? FONT_NUM_B : FONT_NUM);
    const int sz = chip ? 28 : 36;
    char s[4];
    snprintf(s, sizeof(s), "%d", mday % 100);
    text_at(layer, font, x + w / 2, y + h / 2 + (int32_t)lround(sz * 0.36), s, num,
            LV_TEXT_ALIGN_CENTER, 0);
}

/* ---- Left column (mock-up `sideClock`) ---- */

static void draw_clock(lv_layer_t *layer, int32_t ox, int32_t oy, const cal_view_t *v) {
    text_at(layer, FONT_CLOCK, ox + LEFT, oy + CLOCK_BASE, v->time, WS_COLOR_FG,
            LV_TEXT_ALIGN_LEFT, 0);
}

/* "Thu · in 13 days": the middle dot from the bold face, which carries it. */
static void draw_sub(lv_layer_t *layer, int32_t x, int32_t base, const char *s) {
    const char *dot = strstr(s, "\xC2\xB7");
    if (!dot) {
        text_at(layer, FONT_SUB, x, base, s, CAL_SUBTEXT0, LV_TEXT_ALIGN_LEFT, 0);
        return;
    }
    char head[16];
    snprintf(head, sizeof(head), "%.*s", (int)(dot - s), s);
    x += text_at(layer, FONT_SUB, x, base, head, CAL_SUBTEXT0, LV_TEXT_ALIGN_LEFT, 0);
    x += text_at(layer, FONT_SUB_B, x, base, "\xC2\xB7", CAL_SUBTEXT0, LV_TEXT_ALIGN_LEFT, 0);
    text_at(layer, FONT_SUB, x, base, dot + 2, CAL_SUBTEXT0, LV_TEXT_ALIGN_LEFT, 0);
}

static void draw_left(lv_layer_t *layer, int32_t ox, int32_t oy, const cal_view_t *v) {
    draw_clock(layer, ox, oy, v);
    text_at(layer, FONT_DAY, ox + LEFT, oy + DAY_BASE, v->weekday, WS_COLOR_BLUE,
            LV_TEXT_ALIGN_LEFT, 0);
    text_at(layer, FONT_DATE, ox + LEFT, oy + DATE_BASE, v->date, WS_COLOR_FG, LV_TEXT_ALIGN_LEFT,
            0);
    rect(layer, ox + LEFT, oy + RULE_Y - 1, RULE_END - LEFT, 2, WS_COLOR_SURFACE0, 0);
    text_at(layer, FONT_HEAD, ox + LEFT, oy + COMING_BASE, "COMING UP", WS_COLOR_MUTED,
            LV_TEXT_ALIGN_LEFT, 2);
    /* CD-30: an old ts is a muted hint at most, never a reason to blank. */
    if (v->hint[0])
        text_at(layer, FONT_SUB, ox + RULE_END, oy + COMING_BASE, v->hint, WS_COLOR_MUTED,
                LV_TEXT_ALIGN_RIGHT, 0);
    if (v->nup == 0) {
        text_at(layer, FONT_NOTE, ox + LEFT, oy + NOTE_BASE, v->note, WS_COLOR_MUTED,
                LV_TEXT_ALIGN_LEFT, 0);
        return;
    }
    for (int i = 0; i < v->nup; i++) {
        const cal_upcoming_t *u = &v->up[i];
        const int32_t y = oy + UP_Y0 + i * UP_DY;
        draw_cell(layer, ox + LEFT, y, CHIP_W, CHIP_H, u->mday, u->flags, true);
        char label[KDASH_CALENDAR_NAME_MAX + 4];
        fit(FONT_LABEL, u->label, UP_TEXT_MAX - UP_TEXT_X, label, sizeof(label));
        text_at(layer, FONT_LABEL, ox + UP_TEXT_X, y + 22, label, WS_COLOR_FG,
                LV_TEXT_ALIGN_LEFT, 0);
        draw_sub(layer, ox + UP_TEXT_X, y + 46, u->sub);
    }
}

/* ---- Right column (mock-up `cardA`, `grid`) ---- */

static void draw_grid(lv_layer_t *layer, int32_t ox, int32_t oy, const cal_view_t *v) {
    static const char *const DOW[7] = {"S", "M", "T", "W", "T", "F", "S"};
    text_at(layer, FONT_TITLE, ox + GRID_X, oy + TITLE_BASE, v->title, WS_COLOR_BLUE,
            LV_TEXT_ALIGN_LEFT, 0);
    if (v->rows < 1)
        return;
    const double cw = (GRID_W - 6.0 * GRID_GAP) / 7.0;
    const double ch = (GRID_H - (v->rows - 1.0) * GRID_GAP) / v->rows;
    for (int i = 0; i < 7; i++)
        text_at(layer, FONT_HEAD, ox + GRID_X + (int32_t)lround(i * (cw + GRID_GAP) + cw / 2),
                oy + GRID_Y - 12, DOW[i], WS_COLOR_MUTED, LV_TEXT_ALIGN_CENTER, 1);
    for (int d = 1; d <= v->ndays; d++) {
        const int k = v->first_dow + d - 1;
        /* Round both edges, not the size, so the gaps stay even. */
        const double fx = GRID_X + (k % 7) * (cw + GRID_GAP), fy = GRID_Y + (k / 7) * (ch + GRID_GAP);
        const int32_t x0 = (int32_t)lround(fx), y0 = (int32_t)lround(fy);
        const int32_t x1 = (int32_t)lround(fx + cw), y1 = (int32_t)lround(fy + ch);
        draw_cell(layer, ox + x0, oy + y0, x1 - x0, y1 - y0, d, v->cell[d - 1], false);
    }
}

/* ---- The card ---- */

static void draw_card(lv_layer_t *layer, const lv_area_t *a, const cal_view_t *v) {
    /* The mock-up's frame origin: the object is CELL_PAD inside it. */
    const int32_t ox = a->x1 - CELL_PAD, oy = a->y1 - CELL_PAD;
    rect(layer, a->x1, a->y1, CAL_CARD_W, CAL_CARD_H, CAL_CRUST, 0);
    rect(layer, a->x1, a->y1, CAL_CARD_W, CAL_CARD_H, WS_COLOR_BG, 18);
    draw_left(layer, ox, oy, v);
    draw_grid(layer, ox, oy, v);
}

static void draw_cb(lv_event_t *e) {
    lv_obj_t *obj = lv_event_get_target(e);
    cal_widget_t *w = lv_obj_get_user_data(obj);
    if (!w || !w->valid)
        return;
    lv_area_t a;
    lv_obj_get_coords(obj, &a);
    draw_card(lv_event_get_layer(e), &a, &w->cur);
}

static void delete_cb(lv_event_t *e) {
    free(lv_obj_get_user_data(lv_event_get_target(e)));
}

/* ---- Public API ---- */

lv_obj_t *calendar_card_create(lv_obj_t *parent) {
    lv_obj_t *o = lv_obj_create(parent);
    lv_obj_remove_style_all(o);
    lv_obj_clear_flag(o, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_size(o, CAL_CARD_W, CAL_CARD_H);
    cal_widget_t *w = calloc(1, sizeof(*w));
    lv_obj_set_user_data(o, w);
    lv_obj_add_event_cb(o, delete_cb, LV_EVENT_DELETE, NULL);
    lv_obj_add_event_cb(o, draw_cb, LV_EVENT_DRAW_MAIN, NULL);
    return o;
}

void calendar_card_update(lv_obj_t *card, time_t now) {
    cal_widget_t *w = card ? lv_obj_get_user_data(card) : NULL;
    if (!w)
        return;
    cal_view(now, &w->next);
    if (w->valid && memcmp(&w->next, &w->cur, sizeof(w->cur)) == 0)
        return; /* the 59 seconds of every minute with nothing to draw */

    /* Only the clock moved (the common case, once a minute): repaint just
     * its rectangle, not the whole 1256 × 620 card. */
    bool clock_only = false;
    if (w->valid) {
        char t[sizeof(w->cur.time)];
        memcpy(t, w->next.time, sizeof(t));
        memcpy(w->next.time, w->cur.time, sizeof(t));
        clock_only = memcmp(&w->next, &w->cur, sizeof(w->cur)) == 0;
        memcpy(w->next.time, t, sizeof(t));
    }
    const bool first = !w->valid;
    w->cur = w->next;
    w->valid = true;
    if (first || !clock_only) {
        lv_obj_invalidate(card);
        return;
    }
    lv_area_t c;
    lv_obj_get_coords(card, &c);
    const int32_t fx = c.x1 - CELL_PAD, fy = c.y1 - CELL_PAD;
    lv_area_t s = {fx + CLOCK_X0, fy + CLOCK_Y0, fx + CLOCK_X1, fy + CLOCK_Y1};
    lv_obj_invalidate_area(card, &s);
}
