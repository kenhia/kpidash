/* apt_temps_card.c — Apt-Temps per-zone card widget (WI #364).
 * Renders one card per apttemps registry entry: zone name, temperature in °F
 * (coloured by band), and humidity %. Sits in the footer strip to the right
 * of the service cards. Same footprint as the service card. */
#include "widgets/apt_temps_card.h"

#include "registry.h"
#include "widgets/common.h"

#include <stdio.h>
#include <string.h>

#define AT_BORDER 4
#define AT_RADIUS 8
#define AT_PAD 8
#define AT_WIDTH 220
#define AT_HEIGHT 240

static lv_color_t color_for(apttemps_color_t c) {
    switch (c) {
        case APTTEMPS_COLOR_BLUE:   return WS_COLOR_BLUE;
        case APTTEMPS_COLOR_GREEN:  return WS_COLOR_GREEN;
        case APTTEMPS_COLOR_ORANGE: return WS_COLOR_ORANGE;
        case APTTEMPS_COLOR_RED:    return WS_COLOR_RED;
        case APTTEMPS_COLOR_GRAY:
        default:                    return WS_COLOR_MUTED;
    }
}

static void fmt_temp(char *buf, size_t n, float t) {
    snprintf(buf, n, "%.1f°F", (double)t);
}

static void fmt_hum(char *buf, size_t n, int h) {
    snprintf(buf, n, "%d%%", h);
}

lv_obj_t *apt_temps_card_create(lv_obj_t *parent, struct apttemps_entry *e) {
    if (!parent || !e) return NULL;

    lv_obj_t *cont = lv_obj_create(parent);
    lv_obj_clear_flag(cont, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(cont, AT_WIDTH, AT_HEIGHT);
    lv_obj_set_style_bg_color(cont, lv_color_hex(0x181825), 0);
    lv_obj_set_style_radius(cont, AT_RADIUS, 0);
    lv_obj_set_style_border_color(cont, WS_COLOR_SURFACE1, 0);
    lv_obj_set_style_border_width(cont, AT_BORDER, 0);
    lv_obj_set_style_pad_all(cont, AT_PAD, 0);
    lv_obj_set_flex_flow(cont, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(cont, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);

    lv_obj_t *zone = lv_label_create(cont);
    lv_obj_set_style_text_font(zone, &lv_font_montserrat_bold_24, 0);
    lv_obj_set_style_text_color(zone, WS_COLOR_FG, 0);
    lv_label_set_long_mode(zone, LV_LABEL_LONG_DOT);
    lv_obj_set_width(zone, LV_PCT(95));
    lv_obj_set_style_text_align(zone, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(zone, e->zone[0] ? e->zone : e->slug);

    lv_obj_t *temp = lv_label_create(cont);
    lv_obj_set_style_text_font(temp, &lv_font_montserrat_bold_36, 0);
    lv_obj_set_style_text_color(temp, WS_COLOR_MUTED, 0);
    char tb[32];
    fmt_temp(tb, sizeof(tb), e->temp_f);
    lv_label_set_text(temp, tb);

    lv_obj_t *hum = lv_label_create(cont);
    lv_obj_set_style_text_font(hum, &lv_font_montserrat_bold_24, 0);
    lv_obj_set_style_text_color(hum, WS_COLOR_MUTED, 0);
    char hb[16];
    fmt_hum(hb, sizeof(hb), e->humidity_pct);
    lv_label_set_text(hum, hb);

    e->container = cont;
    e->zone_label = zone;
    e->temp_label = temp;
    e->humidity_label = hum;
    return cont;
}

void apt_temps_card_update(struct apttemps_entry *e, double now) {
    if (!e || !e->container) return;
    apttemps_color_t c = apttemps_color(e, now);
    if (e->zone_label)
        lv_label_set_text(e->zone_label, e->zone[0] ? e->zone : e->slug);
    if (e->temp_label) {
        char tb[32];
        fmt_temp(tb, sizeof(tb), e->temp_f);
        lv_label_set_text(e->temp_label, tb);
        lv_obj_set_style_text_color(e->temp_label, color_for(c), 0);
    }
    if (e->humidity_label) {
        char hb[16];
        fmt_hum(hb, sizeof(hb), e->humidity_pct);
        lv_label_set_text(e->humidity_label, hb);
    }
}
