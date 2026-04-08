#include "fortune.h"

#include "common.h"

/* Fortune intentionally uses a darker BG for visual differentiation */
#define FORTUNE_BG lv_color_hex(0x181825)

lv_obj_t *fortune_widget_create(lv_obj_t *parent) {
    lv_obj_t *cont = lv_obj_create(parent);
    lv_obj_clear_flag(cont, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(cont, FORTUNE_BG, 0);
    lv_obj_set_style_radius(cont, 8, 0);
    lv_obj_set_style_border_color(cont, WS_COLOR_SURFACE1, 0);
    lv_obj_set_style_border_width(cont, 1, 0);
    lv_obj_set_style_pad_all(cont, 10, 0);
    lv_obj_set_flex_flow(cont, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(cont, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t *lbl = lv_label_create(cont);
    lv_label_set_text(lbl, "Loading fortune...");
    lv_obj_set_style_text_color(lbl, WS_COLOR_FG, 0);
    lv_obj_set_style_text_font(lbl, WS_FONT_SMALL, 0);
    lv_obj_set_style_text_align(lbl, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(lbl, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(lbl, LV_PCT(95));
    lv_obj_clear_flag(lbl, LV_OBJ_FLAG_SCROLLABLE);

    return cont;
}

void fortune_widget_update(lv_obj_t *widget, const char *text) {
    if (!widget || !text)
        return;
    lv_obj_t *lbl = lv_obj_get_child(widget, 0);
    if (lbl)
        lv_label_set_text(lbl, text);
}
