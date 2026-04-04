/**
 * gpu_graph.c — GPU compute % + VRAM usage dual-axis line chart (T037)
 *
 * Single lv_chart with two series:
 *   - GPU compute % on primary Y-axis (0-100, green)
 *   - VRAM used MB on secondary Y-axis (0-max, blue)
 *
 * Uses LV_CHART_UPDATE_MODE_SHIFT for scrolling time-series.
 * 60 data points = 5 minutes at 5-second telemetry interval.
 */
#include "gpu_graph.h"
#include "lv_font_custom.h"
#include <stdio.h>

/* Catppuccin Mocha palette */
#define COLOR_BG         lv_color_hex(0x1E1E2E)
#define COLOR_FG         lv_color_hex(0xCDD6F4)
#define COLOR_MUTED      lv_color_hex(0x6C7086)
#define COLOR_SURFACE0   lv_color_hex(0x313244)
#define COLOR_GREEN      lv_color_hex(0xA6E3A1)   /* compute % */
#define COLOR_BLUE       lv_color_hex(0x89B4FA)   /* VRAM MB   */

#define POINT_COUNT      60   /* 5 min at 5s interval */

/* User data stored in the chart object's user_data */
typedef struct {
    lv_chart_series_t *compute_ser;
    lv_chart_series_t *vram_ser;
    lv_obj_t          *title_lbl;
    lv_obj_t          *compute_lbl;
    lv_obj_t          *vram_lbl;
    uint32_t           vram_max;   /* last known VRAM total for axis scaling */
} gpu_graph_data_t;

lv_obj_t *gpu_graph_create(lv_obj_t *parent) {
    /* Container */
    lv_obj_t *cont = lv_obj_create(parent);
    lv_obj_set_style_bg_color(cont, COLOR_BG, 0);
    lv_obj_set_style_bg_opa(cont, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(cont, 1, 0);
    lv_obj_set_style_border_color(cont, COLOR_SURFACE0, 0);
    lv_obj_set_style_radius(cont, 8, 0);
    lv_obj_set_style_pad_all(cont, 12, 0);
    lv_obj_clear_flag(cont, LV_OBJ_FLAG_SCROLLABLE);

    /* Title label */
    lv_obj_t *title = lv_label_create(cont);
    lv_label_set_text(title, "GPU");
    lv_obj_set_style_text_color(title, COLOR_FG, 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_bold_20, 0);
    lv_obj_align(title, LV_ALIGN_TOP_LEFT, 0, 0);

    /* Legend: compute % label (green) */
    lv_obj_t *comp_lbl = lv_label_create(cont);
    lv_label_set_text(comp_lbl, "Compute: ---%");
    lv_obj_set_style_text_color(comp_lbl, COLOR_GREEN, 0);
    lv_obj_set_style_text_font(comp_lbl, &lv_font_montserrat_16, 0);
    lv_obj_align(comp_lbl, LV_ALIGN_TOP_RIGHT, -120, 0);

    /* Legend: VRAM label (blue) */
    lv_obj_t *vram_lbl = lv_label_create(cont);
    lv_label_set_text(vram_lbl, "VRAM: ---- MB");
    lv_obj_set_style_text_color(vram_lbl, COLOR_BLUE, 0);
    lv_obj_set_style_text_font(vram_lbl, &lv_font_montserrat_16, 0);
    lv_obj_align(vram_lbl, LV_ALIGN_TOP_RIGHT, 0, 0);

    /* Chart */
    lv_obj_t *chart = lv_chart_create(cont);
    lv_obj_set_size(chart, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_grow(chart, 1);
    lv_obj_align(chart, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_bg_color(chart, COLOR_BG, 0);
    lv_obj_set_style_bg_opa(chart, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(chart, 0, 0);
    lv_obj_set_style_line_color(chart, COLOR_SURFACE0, LV_PART_MAIN);
    lv_obj_set_style_line_width(chart, 1, LV_PART_MAIN);
    lv_obj_clear_flag(chart, LV_OBJ_FLAG_SCROLLABLE);

    /* Line width for series */
    lv_obj_set_style_line_width(chart, 2, LV_PART_ITEMS);
    lv_obj_set_style_size(chart, 0, 0, LV_PART_INDICATOR); /* hide point dots */

    lv_chart_set_type(chart, LV_CHART_TYPE_LINE);
    lv_chart_set_update_mode(chart, LV_CHART_UPDATE_MODE_SHIFT);
    lv_chart_set_point_count(chart, POINT_COUNT);
    lv_chart_set_div_line_count(chart, 5, 0); /* 5 horizontal grid lines */

    /* Primary Y-axis: compute % (0-100) */
    lv_chart_set_range(chart, LV_CHART_AXIS_PRIMARY_Y, 0, 100);

    /* Secondary Y-axis: VRAM MB (default 0-16384, updated dynamically) */
    lv_chart_set_range(chart, LV_CHART_AXIS_SECONDARY_Y, 0, 16384);

    /* Add series */
    lv_chart_series_t *compute_ser = lv_chart_add_series(chart, COLOR_GREEN,
                                                          LV_CHART_AXIS_PRIMARY_Y);
    lv_chart_series_t *vram_ser    = lv_chart_add_series(chart, COLOR_BLUE,
                                                          LV_CHART_AXIS_SECONDARY_Y);

    /* Initialize all points to 0 */
    lv_chart_set_all_value(chart, compute_ser, 0);
    lv_chart_set_all_value(chart, vram_ser, 0);

    /* Use flex layout so chart fills remaining space below title */
    lv_obj_set_flex_flow(cont, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(cont, 4, 0);

    /* Store series handles in user_data */
    gpu_graph_data_t *data = lv_malloc(sizeof(gpu_graph_data_t));
    data->compute_ser = compute_ser;
    data->vram_ser    = vram_ser;
    data->title_lbl   = title;
    data->compute_lbl = comp_lbl;
    data->vram_lbl    = vram_lbl;
    data->vram_max    = 16384;
    lv_obj_set_user_data(cont, data);

    return cont;
}

void gpu_graph_update(lv_obj_t *graph, float compute_pct,
                      uint32_t vram_used_mb, uint32_t vram_total_mb) {
    if (!graph) return;
    gpu_graph_data_t *data = lv_obj_get_user_data(graph);
    if (!data) return;

    /* Find the chart (first child with chart class — it's the last flex child) */
    lv_obj_t *chart = NULL;
    uint32_t cnt = lv_obj_get_child_count(graph);
    for (uint32_t i = 0; i < cnt; i++) {
        lv_obj_t *child = lv_obj_get_child(graph, i);
        if (lv_obj_check_type(child, &lv_chart_class)) {
            chart = child;
            break;
        }
    }
    if (!chart) return;

    /* Update secondary Y-axis range if VRAM total changed */
    if (vram_total_mb > 0 && vram_total_mb != data->vram_max) {
        data->vram_max = vram_total_mb;
        lv_chart_set_range(chart, LV_CHART_AXIS_SECONDARY_Y,
                           0, (int32_t)vram_total_mb);
    }

    /* Push new data points */
    int32_t comp_val = (int32_t)(compute_pct + 0.5f);
    if (comp_val < 0)   comp_val = 0;
    if (comp_val > 100)  comp_val = 100;
    lv_chart_set_next_value(chart, data->compute_ser, comp_val);
    lv_chart_set_next_value(chart, data->vram_ser, (int32_t)vram_used_mb);

    /* Update legend labels */
    char buf[48];
    snprintf(buf, sizeof(buf), "Compute: %d%%", comp_val);
    lv_label_set_text(data->compute_lbl, buf);

    snprintf(buf, sizeof(buf), "VRAM: %u / %u MB", vram_used_mb, vram_total_mb);
    lv_label_set_text(data->vram_lbl, buf);

    lv_chart_refresh(chart);
}

void gpu_graph_destroy(lv_obj_t *graph) {
    if (!graph) return;
    gpu_graph_data_t *data = lv_obj_get_user_data(graph);
    if (data) lv_free(data);
    lv_obj_delete(graph);
}
