#include "dev_textsize.h"

#include <stdio.h>

#define PANEL_COLOR lv_color_hex(0x1E1E2E)
#define TEXT_COLOR lv_color_hex(0xCDD6F4)
#define PAD 12

/* font sizes available in lv_conf.h for this project */
static const struct {
    const lv_font_t *font;
    const char label[8];
} kFonts[] = {
    {&lv_font_montserrat_14, "14px"}, {&lv_font_montserrat_16, "16px"},
    {&lv_font_montserrat_20, "20px"}, {&lv_font_montserrat_24, "24px"},
    {&lv_font_montserrat_28, "28px"},
};
#define N_FONTS ((int)(sizeof(kFonts) / sizeof(kFonts[0])))

lv_obj_t *dev_textsize_create(lv_obj_t *parent) {
    lv_obj_t *panel = lv_obj_create(parent);
    lv_obj_set_style_bg_color(panel, PANEL_COLOR, 0);
    lv_obj_set_style_bg_opa(panel, LV_OPA_90, 0);
    lv_obj_set_style_border_color(panel, lv_color_hex(0x45475A), 0);
    lv_obj_set_style_border_width(panel, 1, 0);
    lv_obj_set_style_radius(panel, 8, 0);
    lv_obj_set_style_pad_all(panel, PAD, 0);
    lv_obj_set_style_pad_row(panel, 6, 0);
    lv_obj_set_width(panel, LV_SIZE_CONTENT);
    lv_obj_set_height(panel, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(panel, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(panel, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_clear_flag(panel, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_align(panel, LV_ALIGN_TOP_RIGHT, -8, 8);
    lv_obj_move_foreground(panel);

    char buf[64];
    for (int i = 0; i < N_FONTS; i++) {
        snprintf(buf, sizeof(buf), "%s  The quick brown fox", kFonts[i].label);
        lv_obj_t *lbl = lv_label_create(panel);
        lv_label_set_text(lbl, buf);
        lv_obj_set_style_text_color(lbl, TEXT_COLOR, 0);
        lv_obj_set_style_text_font(lbl, kFonts[i].font, 0);
        lv_obj_clear_flag(lbl, LV_OBJ_FLAG_SCROLLABLE);
    }

    return panel;
}

void dev_textsize_destroy(lv_obj_t *panel) {
    if (panel)
        lv_obj_delete(panel);
}
