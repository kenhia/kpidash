#include "status_bar.h"
#include <stdio.h>
#include <string.h>

#define COLOR_WARN  lv_color_hex(0xF9E2AF)  /* catppuccin yellow */
#define COLOR_ERROR lv_color_hex(0xF38BA8)  /* catppuccin red    */
#define COLOR_BG    lv_color_hex(0x1E1E2E)

lv_obj_t *status_bar_create(lv_obj_t *parent) {
    lv_obj_t *bar = lv_obj_create(parent);
    lv_obj_clear_flag(bar, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(bar, COLOR_BG, 0);
    lv_obj_set_style_radius(bar, 4, 0);
    lv_obj_set_style_border_width(bar, 1, 0);
    lv_obj_set_style_border_color(bar, lv_color_hex(0x45475A), 0);
    lv_obj_set_style_pad_all(bar, 6, 0);
    lv_obj_set_flex_flow(bar, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(bar, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                           LV_FLEX_ALIGN_START);
    lv_obj_add_flag(bar, LV_OBJ_FLAG_HIDDEN);  /* hidden until needed */

    lv_obj_t *lbl = lv_label_create(bar);
    lv_label_set_text(lbl, "");
    lv_obj_set_style_text_color(lbl, COLOR_WARN, 0);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_14, 0);
    lv_label_set_long_mode(lbl, LV_LABEL_LONG_CLIP);
    lv_obj_set_width(lbl, LV_PCT(100));
    lv_obj_clear_flag(lbl, LV_OBJ_FLAG_SCROLLABLE);

    return bar;
}

void status_bar_show(lv_obj_t *bar, status_severity_t severity, const char *message) {
    if (!bar) return;
    lv_obj_t *lbl = lv_obj_get_child(bar, 0);
    if (lbl) {
        char buf[300];
        snprintf(buf, sizeof(buf), "[%s] %s",
                 severity == STATUS_ERROR ? "ERROR" : "WARN",
                 message);
        lv_label_set_text(lbl, buf);
        lv_color_t color = severity == STATUS_ERROR ? COLOR_ERROR : COLOR_WARN;
        lv_obj_set_style_text_color(lbl, color, 0);
        lv_obj_set_style_border_color(bar, color, 0);
    }
    lv_obj_clear_flag(bar, LV_OBJ_FLAG_HIDDEN);
}

void status_bar_hide(lv_obj_t *bar) {
    if (!bar) return;
    lv_obj_add_flag(bar, LV_OBJ_FLAG_HIDDEN);
}
