#include "dev_grid.h"

#include <stdio.h>

#include "layout.h"

#define GRID_LINE_COLOR lv_color_hex(0xFFFFFF)
#define GRID_LINE_OPA LV_OPA_20
#define GRID_LINE_WIDTH 1

static void add_vline(lv_obj_t *overlay, int32_t x, int32_t scr_h) {
    lv_obj_t *line = lv_obj_create(overlay);
    lv_obj_set_size(line, GRID_LINE_WIDTH, scr_h);
    lv_obj_set_pos(line, x, 0);
    lv_obj_set_style_bg_color(line, GRID_LINE_COLOR, 0);
    lv_obj_set_style_bg_opa(line, GRID_LINE_OPA, 0);
    lv_obj_set_style_border_width(line, 0, 0);
    lv_obj_set_style_pad_all(line, 0, 0);
    lv_obj_clear_flag(line, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
}

static void add_hline(lv_obj_t *overlay, int32_t y, int32_t scr_w) {
    lv_obj_t *line = lv_obj_create(overlay);
    lv_obj_set_size(line, scr_w, GRID_LINE_WIDTH);
    lv_obj_set_pos(line, 0, y);
    lv_obj_set_style_bg_color(line, GRID_LINE_COLOR, 0);
    lv_obj_set_style_bg_opa(line, GRID_LINE_OPA, 0);
    lv_obj_set_style_border_width(line, 0, 0);
    lv_obj_set_style_pad_all(line, 0, 0);
    lv_obj_clear_flag(line, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
}

lv_obj_t *dev_grid_create(lv_obj_t *parent, int grid_size, bool unit_mode, float unit_size) {
    lv_disp_t *disp = lv_display_get_default();
    int32_t scr_w = lv_display_get_horizontal_resolution(disp);
    int32_t scr_h = lv_display_get_vertical_resolution(disp);

    /* Full-screen transparent overlay — floats above everything */
    lv_obj_t *overlay = lv_obj_create(parent);
    lv_obj_set_size(overlay, scr_w, scr_h);
    lv_obj_set_pos(overlay, 0, 0);
    lv_obj_set_style_bg_opa(overlay, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(overlay, 0, 0);
    lv_obj_set_style_pad_all(overlay, 0, 0);
    lv_obj_clear_flag(overlay, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    /* Raise to top of z-order */
    lv_obj_move_foreground(overlay);

    if (unit_mode) {
        /* Unit-based grid: lines at interior cell boundaries (gap midpoints) */
        if (unit_size < 0.1f)
            unit_size = 0.5f; /* T012: clamp size:0 to minimum */
        float step_w = unit_size * (float)UNIT_W;
        float step_h = unit_size * (float)UNIT_H;

        /* Vertical lines: skip screen edges (first at PAD_LEFT + step, last before PAD_LEFT +
         * COLS*UNIT_W) */
        for (float fx = (float)PAD_LEFT + step_w; fx < (float)(PAD_LEFT + COLS * UNIT_W) - 0.5f;
             fx += step_w) {
            add_vline(overlay, (int32_t)(fx + 0.5f), scr_h);
        }

        /* Horizontal lines: skip screen edges */
        for (float fy = (float)PAD_TOP + step_h; fy < (float)(PAD_TOP + ROWS * UNIT_H) - 0.5f;
             fy += step_h) {
            add_hline(overlay, (int32_t)(fy + 0.5f), scr_w);
        }
    } else {
        /* Pixel-based grid (original behavior) */
        if (grid_size <= 0)
            grid_size = 50;

        for (int32_t x = grid_size; x < scr_w; x += grid_size) add_vline(overlay, x, scr_h);

        for (int32_t y = grid_size; y < scr_h; y += grid_size) add_hline(overlay, y, scr_w);
    }

    return overlay;
}

void dev_grid_destroy(lv_obj_t *grid) {
    if (grid)
        lv_obj_delete(grid);
}
