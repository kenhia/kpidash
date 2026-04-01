#include "repo_status.h"
#include <string.h>
#include <stdio.h>

#define COLOR_BG     lv_color_hex(0x1E1E2E)
#define COLOR_FG     lv_color_hex(0xCDD6F4)
#define COLOR_MUTED  lv_color_hex(0x6C7086)
#define COLOR_DIRTY  lv_color_hex(0xFAB387)
#define COLOR_BRANCH lv_color_hex(0xCBA6F7)
#define COLOR_HEADER lv_color_hex(0xF5C2E7)
#define COLOR_CLEAN  lv_color_hex(0xA6E3A1)

static const lv_font_t *FONT_HDR  = &lv_font_montserrat_16;
static const lv_font_t *FONT_BODY = &lv_font_montserrat_14;

lv_obj_t *repo_status_widget_create(lv_obj_t *parent) {
    lv_obj_t *cont = lv_obj_create(parent);
    lv_obj_clear_flag(cont, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(cont, COLOR_BG, 0);
    lv_obj_set_style_radius(cont, 8, 0);
    lv_obj_set_style_border_color(cont, lv_color_hex(0x45475A), 0);
    lv_obj_set_style_border_width(cont, 1, 0);
    lv_obj_set_style_pad_all(cont, 10, 0);
    lv_obj_set_style_pad_row(cont, 4, 0);
    lv_obj_set_flex_flow(cont, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(cont, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_START);

    /* Header */
    lv_obj_t *hdr = lv_label_create(cont);
    lv_label_set_text(hdr, "Repo Status");
    lv_obj_set_style_text_color(hdr, COLOR_HEADER, 0);
    lv_obj_set_style_text_font(hdr, FONT_HDR, 0);
    lv_obj_clear_flag(hdr, LV_OBJ_FLAG_SCROLLABLE);

    /* Initial placeholder */
    lv_obj_t *clean = lv_label_create(cont);
    lv_label_set_text(clean, "All repos clean");
    lv_obj_set_style_text_color(clean, COLOR_CLEAN, 0);
    lv_obj_set_style_text_font(clean, FONT_BODY, 0);
    lv_obj_clear_flag(clean, LV_OBJ_FLAG_SCROLLABLE);

    return cont;
}

void repo_status_widget_update(lv_obj_t *widget, const repo_entry_t *list, int count) {
    /* Remove all children after header */
    uint32_t n = lv_obj_get_child_count(widget);
    for (uint32_t i = n; i > 1; i--) {
        lv_obj_delete(lv_obj_get_child(widget, i - 1));
    }

    if (count == 0) {
        lv_obj_t *clean = lv_label_create(widget);
        lv_label_set_text(clean, "All repos clean");
        lv_obj_set_style_text_color(clean, COLOR_CLEAN, 0);
        lv_obj_set_style_text_font(clean, FONT_BODY, 0);
        lv_obj_clear_flag(clean, LV_OBJ_FLAG_SCROLLABLE);
        return;
    }

    for (int i = 0; i < count; i++) {
        const repo_entry_t *r = &list[i];

        /* Row */
        lv_obj_t *row = lv_obj_create(widget);
        lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(row, 0, 0);
        lv_obj_set_style_pad_all(row, 0, 0);
        lv_obj_set_width(row, LV_PCT(100));
        lv_obj_set_height(row, LV_SIZE_CONTENT);
        lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                               LV_FLEX_ALIGN_START);
        lv_obj_set_style_pad_column(row, 6, 0);

        /* Dirty indicator */
        if (r->is_dirty) {
            lv_obj_t *dot = lv_label_create(row);
            lv_label_set_text(dot, "●");
            lv_obj_set_style_text_color(dot, COLOR_DIRTY, 0);
            lv_obj_set_style_text_font(dot, FONT_BODY, 0);
            lv_obj_clear_flag(dot, LV_OBJ_FLAG_SCROLLABLE);
        }

        /* host:name */
        char info[256];
        snprintf(info, sizeof(info), "%s/%s", r->host, r->name);
        lv_obj_t *name_lbl = lv_label_create(row);
        lv_label_set_text(name_lbl, info);
        lv_obj_set_style_text_color(name_lbl, COLOR_FG, 0);
        lv_obj_set_style_text_font(name_lbl, FONT_BODY, 0);
        lv_obj_set_flex_grow(name_lbl, 1);
        lv_label_set_long_mode(name_lbl, LV_LABEL_LONG_CLIP);
        lv_obj_clear_flag(name_lbl, LV_OBJ_FLAG_SCROLLABLE);

        /* Branch */
        lv_obj_t *branch_lbl = lv_label_create(row);
        lv_label_set_text(branch_lbl, r->branch);
        lv_obj_set_style_text_color(branch_lbl, COLOR_BRANCH, 0);
        lv_obj_set_style_text_font(branch_lbl, FONT_BODY, 0);
        lv_obj_clear_flag(branch_lbl, LV_OBJ_FLAG_SCROLLABLE);
    }
}
