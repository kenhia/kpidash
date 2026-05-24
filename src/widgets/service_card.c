/* service_card.c — Service Status Card widget (T024).
 * Renders one card per registry entry: thick coloured border + optional
 * icon + name + status text. Border colour is driven by service_color(). */
#include "widgets/service_card.h"

#include "icons.h"
#include "registry.h"
#include "widgets/common.h"

#include <string.h>

#define SC_BORDER_THICK 6
#define SC_RADIUS 8
#define SC_PAD 8
#define SC_WIDTH 220
#define SC_HEIGHT 240

static lv_color_t color_for(service_color_t c) {
    switch (c) {
        case SERVICE_COLOR_GREEN:  return lv_color_hex(0x40A02B);
        case SERVICE_COLOR_YELLOW: return lv_color_hex(0xDF8E1D);
        case SERVICE_COLOR_BLUE:   return lv_color_hex(0x1E66F5);
        case SERVICE_COLOR_RED:    return lv_color_hex(0xD20F39);
        case SERVICE_COLOR_GRAY:
        default:                   return lv_color_hex(0x6C7086);
    }
}

lv_obj_t *service_card_create(lv_obj_t *parent, struct service_entry *e) {
    if (!parent || !e) return NULL;

    lv_obj_t *cont = lv_obj_create(parent);
    lv_obj_clear_flag(cont, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(cont, SC_WIDTH, SC_HEIGHT);
    lv_obj_set_style_bg_color(cont, lv_color_hex(0x181825), 0);
    lv_obj_set_style_radius(cont, SC_RADIUS, 0);
    lv_obj_set_style_border_color(cont, color_for(SERVICE_COLOR_GRAY), 0);
    lv_obj_set_style_border_width(cont, SC_BORDER_THICK, 0);
    lv_obj_set_style_pad_all(cont, SC_PAD, 0);
    lv_obj_set_flex_flow(cont, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(cont, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);

    lv_obj_t *icon = lv_label_create(cont);
    lv_obj_set_style_text_font(icon, &lv_font_icons_56, 0);
    lv_obj_set_style_text_color(icon, WS_COLOR_FG, 0);
    const char *glyph = icons_lookup(e->icon_index);
    if (glyph) {
        lv_label_set_text(icon, glyph);
    } else {
        lv_obj_add_flag(icon, LV_OBJ_FLAG_HIDDEN);
    }

    lv_obj_t *name = lv_label_create(cont);
    lv_obj_set_style_text_font(name, &lv_font_montserrat_bold_24, 0);
    lv_obj_set_style_text_color(name, WS_COLOR_FG, 0);
    lv_label_set_long_mode(name, LV_LABEL_LONG_DOT);
    lv_obj_set_width(name, LV_PCT(95));
    lv_obj_set_style_text_align(name, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(name, e->name[0] ? e->name : "(service)");

    lv_obj_t *host = lv_label_create(cont);
    lv_obj_set_style_text_font(host, &lv_font_montserrat_bold_16, 0);
    lv_obj_set_style_text_color(host, lv_color_hex(0xA6ADC8), 0);
    lv_label_set_long_mode(host, LV_LABEL_LONG_DOT);
    lv_obj_set_width(host, LV_PCT(95));
    lv_obj_set_style_text_align(host, LV_TEXT_ALIGN_CENTER, 0);
    if (e->host[0] && strcmp(e->host, "_") != 0) {
        lv_label_set_text(host, e->host);
    } else {
        lv_obj_add_flag(host, LV_OBJ_FLAG_HIDDEN);
    }

    lv_obj_t *text = lv_label_create(cont);
    lv_obj_set_style_text_font(text, &lv_font_montserrat_bold_20, 0);
    lv_obj_set_style_text_color(text, WS_COLOR_FG, 0);
    lv_label_set_long_mode(text, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(text, LV_PCT(95));
    lv_obj_set_style_text_align(text, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(text, e->text[0] ? e->text : "");

    e->container = cont;
    e->border = cont;
    e->icon_label = icon;
    e->name_label = name;
    e->host_label = host;
    e->text_label = text;
    return cont;
}

void service_card_update(struct service_entry *e, double now) {
    if (!e || !e->container) return;
    service_color_t c = service_color(e, now);
    lv_obj_set_style_border_color(e->container, color_for(c), 0);
    if (e->text_label) lv_label_set_text(e->text_label, e->text);
    if (e->name_label) lv_label_set_text(e->name_label, e->name[0] ? e->name : "(service)");
    if (e->host_label) {
        if (e->host[0] && strcmp(e->host, "_") != 0) {
            lv_label_set_text(e->host_label, e->host);
            lv_obj_clear_flag(e->host_label, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(e->host_label, LV_OBJ_FLAG_HIDDEN);
        }
    }
    if (e->icon_label) {
        const char *glyph = icons_lookup(e->icon_index);
        if (glyph) {
            lv_label_set_text(e->icon_label, glyph);
            lv_obj_clear_flag(e->icon_label, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(e->icon_label, LV_OBJ_FLAG_HIDDEN);
        }
    }
}
