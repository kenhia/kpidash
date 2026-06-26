#include "fortune.h"

#include <stdio.h>

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

static const lv_font_t *pt_to_font(int pt) {
    switch (pt) {
        case 20: return &lv_font_montserrat_bold_20;
        case 24: return &lv_font_montserrat_bold_24;
        case 28: return &lv_font_montserrat_bold_28;
        default: return &lv_font_montserrat_bold_36;
    }
}

void fortune_widget_update(lv_obj_t *widget, const char *text, int font_size_pt) {
    if (!widget || !text)
        return;
    lv_obj_t *lbl = lv_obj_get_child(widget, 0);
    if (!lbl)
        return;
    lv_obj_set_style_text_font(lbl, pt_to_font(font_size_pt), 0);
    lv_label_set_text(lbl, text);
}

#include "layout.h"

/* Sprint 006 / T014: pool-placeable Fortune renderer.
 * 2×1 cell (UNIT_W_N(2) × UNIT_H_N(1)) at absolute (x, y) with larger font. */
lv_obj_t *fortune_create(lv_obj_t *parent, lv_coord_t x, lv_coord_t y) {
    lv_obj_t *cont = lv_obj_create(parent);
    lv_obj_clear_flag(cont, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(cont, UNIT_W_N(2), UNIT_H_N(1));
    lv_obj_set_pos(cont, x, y);
    lv_obj_set_style_bg_color(cont, FORTUNE_BG, 0);
    lv_obj_set_style_radius(cont, 8, 0);
    lv_obj_set_style_border_color(cont, WS_COLOR_SURFACE1, 0);
    lv_obj_set_style_border_width(cont, 1, 0);
    lv_obj_set_style_pad_all(cont, 16, 0);
    lv_obj_set_flex_flow(cont, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(cont, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t *lbl = lv_label_create(cont);
    lv_label_set_text(lbl, "Loading fortune...");
    lv_obj_set_style_text_color(lbl, WS_COLOR_FG, 0);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_bold_36, 0);
    lv_obj_set_style_text_align(lbl, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(lbl, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(lbl, LV_PCT(95));
    lv_obj_clear_flag(lbl, LV_OBJ_FLAG_SCROLLABLE);

    /* Dev-stats overlay (child 1): shown only when kpidash:cmd:fortune_dev is set */
    lv_obj_t *dev_lbl = lv_label_create(cont);
    lv_label_set_text(dev_lbl, "");
    lv_obj_set_style_text_color(dev_lbl, WS_COLOR_BLUE, 0);
    lv_obj_set_style_text_font(dev_lbl, &lv_font_montserrat_bold_14, 0);
    lv_obj_set_pos(dev_lbl, 0, 0);
    lv_obj_add_flag(dev_lbl, LV_OBJ_FLAG_IGNORE_LAYOUT | LV_OBJ_FLAG_HIDDEN);
    
    /* Move dev_lbl to front (last child renders on top) */
    lv_obj_move_to_index(dev_lbl, -1);

    return cont;
}

void fortune_widget_set_dev_stats(lv_obj_t *widget, bool show, int rejects, long elapsed_ms) {
    if (!widget)
        return;
    lv_obj_t *dev_lbl = lv_obj_get_child(widget, 1);
    if (!dev_lbl)
        return;
    if (!show) {
        lv_obj_add_flag(dev_lbl, LV_OBJ_FLAG_HIDDEN);
        return;
    }
    char buf[64];
    snprintf(buf, sizeof(buf), "Rejects: %d  Elapsed: %ld ms", rejects, elapsed_ms);
    lv_label_set_text(dev_lbl, buf);
    lv_obj_clear_flag(dev_lbl, LV_OBJ_FLAG_HIDDEN);
}
