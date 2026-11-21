/**
 * apt_temps_card.c — the apt-temps widget (WI #4848, sprint 047). See the
 * header.
 *
 * Coordinates are the object's own, 368 × 620, with text y values as
 * baselines (text_at() converts them to LVGL's top-of-line), the convention
 * the other row-3 cards use. Every fill is opaque (sprint 030).
 */
#include "widgets/apt_temps_card.h"

#include <stdlib.h>
#include <string.h>

#include "atstate.h"
#include "registry.h"
#include "widgets/common.h"

#define AT_SUBTEXT0 lv_color_hex(0xA6ADC8)

/* WI #4870 (sprint 048): bold 48, the 10G card's verdict face. Ken found the
 * 96 px digits too big and asked for a face between the two if the repo had
 * one, else 48 — it has none (14-36, 48, then 96 digits only), and his rule
 * was "use what we have", so no size is generated for this. */
#define FONT_TEMP (&lv_font_montserrat_bold_48)
#define FONT_NAME (&lv_font_montserrat_bold_28)
#define FONT_HUM (&lv_font_montserrat_bold_28)
#define FONT_NOTE (&lv_font_montserrat_24)

#define CARD_R 20
#define BORDER_W 4 /* the green border: a fresh sensor nobody has named */
#define ZONE_H (AT_CARD_H / AT_ZONES) /* 206 */
/* WI #4870: zone, temperature, humidity — the old footer cards' order — each
 * centred, the stack centred in its 206 px zone. Line boxes: name 35, temp 59,
 * humidity 35, with 6 px gaps; the stack sits 4 px below centre, which centres
 * the glyphs (a line box carries more leading above them than below). Baselines
 * within a zone: */
#define NAME_BASE 62
#define TEMP_BASE 129
#define HUM_BASE 174
#define RULE_X 24 /* the rules between zones stop short of the card's edge */
#define RULE_H 2

typedef struct {
    at_view_t cur;
    at_view_t next; /* scratch: at_view zeroes it, so memcmp sees only content */
    bool valid;
} at_widget_t;

static lv_color_t band_color(apttemps_color_t c) {
    switch (c) {
    case APTTEMPS_COLOR_BLUE:
        return WS_COLOR_BLUE;
    case APTTEMPS_COLOR_GREEN:
        return WS_COLOR_GREEN;
    case APTTEMPS_COLOR_ORANGE:
        return WS_COLOR_ORANGE;
    case APTTEMPS_COLOR_RED:
        return WS_COLOR_RED;
    case APTTEMPS_COLOR_GRAY:
    default:
        return WS_COLOR_MUTED;
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

/* Text whose BASELINE is at y. `x` is the left edge, the centre for
 * LV_TEXT_ALIGN_CENTER, or the right edge for LV_TEXT_ALIGN_RIGHT. */
static void text_at(lv_layer_t *layer, const lv_font_t *font, int32_t x, int32_t baseline,
                    const char *s, lv_color_t c, lv_text_align_t align) {
    if (!s || !s[0])
        return;
    int32_t w = lv_text_get_width(s, (uint32_t)strlen(s), font, 0);
    if (align == LV_TEXT_ALIGN_CENTER)
        x -= w / 2;
    else if (align == LV_TEXT_ALIGN_RIGHT)
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
}

/* ---- The pieces ---- */

static void draw_zone(lv_layer_t *layer, int32_t x0, int32_t y0, int i, const at_zone_t *z) {
    const int32_t cx = x0 + AT_CARD_W / 2;
    if (!z->present) {
        /* No key: the slot keeps its shape, everything muted. */
        text_at(layer, FONT_NAME, cx, y0 + NAME_BASE, at_zone_name(i), WS_COLOR_MUTED,
                LV_TEXT_ALIGN_CENTER);
        text_at(layer, FONT_TEMP, cx, y0 + TEMP_BASE, "\xE2\x80\x94", WS_COLOR_SURFACE1,
                LV_TEXT_ALIGN_CENTER);
        text_at(layer, FONT_NOTE, cx, y0 + HUM_BASE, "no reading", WS_COLOR_MUTED,
                LV_TEXT_ALIGN_CENTER);
        return;
    }
    const bool stale = z->color == APTTEMPS_COLOR_GRAY;
    text_at(layer, FONT_NAME, cx, y0 + NAME_BASE, at_zone_name(i),
            stale ? AT_SUBTEXT0 : WS_COLOR_FG, LV_TEXT_ALIGN_CENTER);
    text_at(layer, FONT_TEMP, cx, y0 + TEMP_BASE, z->temp, band_color(z->color),
            LV_TEXT_ALIGN_CENTER);
    text_at(layer, FONT_HUM, cx, y0 + HUM_BASE, z->hum, stale ? WS_COLOR_MUTED : AT_SUBTEXT0,
            LV_TEXT_ALIGN_CENTER);
}

static void draw_card(lv_layer_t *layer, const lv_area_t *a, const at_view_t *v) {
    const int32_t x0 = a->x1, y0 = a->y1;
    rect(layer, x0, y0, AT_CARD_W, AT_CARD_H, lv_color_hex(0x11111B), 0);
    if (v->unknown) {
        /* A ring is the green rounded rect with the card's base inset by
         * the border's width: two opaque fills, no border blend. */
        rect(layer, x0, y0, AT_CARD_W, AT_CARD_H, WS_COLOR_GREEN, CARD_R);
        rect(layer, x0 + BORDER_W, y0 + BORDER_W, AT_CARD_W - 2 * BORDER_W,
             AT_CARD_H - 2 * BORDER_W, WS_COLOR_BG, CARD_R - BORDER_W);
    } else {
        rect(layer, x0, y0, AT_CARD_W, AT_CARD_H, WS_COLOR_BG, CARD_R);
    }
    for (int i = 0; i < AT_ZONES; i++) {
        const int32_t zy = y0 + i * ZONE_H;
        if (i > 0)
            rect(layer, x0 + RULE_X, zy - RULE_H / 2, AT_CARD_W - 2 * RULE_X, RULE_H,
                 WS_COLOR_SURFACE0, 0);
        draw_zone(layer, x0, zy, i, &v->zone[i]);
    }
}

static void draw_cb(lv_event_t *e) {
    lv_obj_t *obj = lv_event_get_target(e);
    at_widget_t *w = lv_obj_get_user_data(obj);
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

lv_obj_t *apt_temps_card_create(lv_obj_t *parent) {
    lv_obj_t *o = lv_obj_create(parent);
    lv_obj_remove_style_all(o);
    lv_obj_clear_flag(o, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_size(o, AT_CARD_W, AT_CARD_H);
    at_widget_t *w = calloc(1, sizeof(*w));
    lv_obj_set_user_data(o, w);
    lv_obj_add_event_cb(o, delete_cb, LV_EVENT_DELETE, NULL);
    lv_obj_add_event_cb(o, draw_cb, LV_EVENT_DRAW_MAIN, NULL);
    return o;
}

void apt_temps_card_update(lv_obj_t *card, double now) {
    at_widget_t *w = card ? lv_obj_get_user_data(card) : NULL;
    if (!w)
        return;
    static apttemps_entry_t snap[APTTEMPS_REGISTRY_MAX];
    int n = apttemps_registry_snapshot(snap, APTTEMPS_REGISTRY_MAX);
    at_view(snap, n, now, &w->next);
    if (w->valid && memcmp(&w->next, &w->cur, sizeof(w->cur)) == 0)
        return;
    w->cur = w->next;
    w->valid = true;
    lv_obj_invalidate(card);
}
