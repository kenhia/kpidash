/**
 * dev_graph.c — 5-series dev telemetry line chart (Phase 6.1)
 *
 * Single lv_chart with 5 series on two Y-axes:
 *   Primary Y (0-100%):  GPU compute, CPU avg, CPU top core
 *   Secondary Y (0-max MB): VRAM used, RAM used
 *
 * 300 data points = 5 min at 1s dev telemetry interval.
 * Sized to two cell slots (UNIT_W_N(2) x UNIT_H).
 *
 * Header row:
 *   GPU (left)                hostname (center)            System (right)
 *   Compute: X%                                            CPU: X% / Y%
 *   VRAM: X / Y MB                                         RAM: X / Y GB
 */
#include "dev_graph.h"

#include <stdio.h>
#include <string.h>

#include "common.h"
#include "layout.h"

/* Graph series colors (widget-specific aliases from shared palette) */
#define COLOR_GREEN WS_COLOR_GREEN   /* GPU compute % */
#define COLOR_BLUE WS_COLOR_BLUE     /* CPU avg % */
#define COLOR_PURPLE WS_COLOR_MAUVE  /* CPU top core % */
#define COLOR_ORANGE WS_COLOR_ORANGE /* VRAM MB */
#define COLOR_RED WS_COLOR_RED       /* RAM MB */

#define POINT_COUNT 300 /* 5 min at 1s interval */

typedef struct {
    lv_chart_series_t *gpu_compute_ser;
    lv_chart_series_t *cpu_avg_ser;
    lv_chart_series_t *cpu_top_ser;
    lv_chart_series_t *vram_ser;
    lv_chart_series_t *ram_ser;
    lv_obj_t *gpu_title;
    lv_obj_t *gpu_compute_lbl;
    lv_obj_t *gpu_vram_lbl;
    lv_obj_t *host_lbl;
    lv_obj_t *sys_title;
    lv_obj_t *sys_cpu_lbl;
    lv_obj_t *sys_ram_lbl;
    uint32_t vram_max;
    uint32_t ram_max;
} dev_graph_priv_t;

lv_obj_t *dev_graph_create(lv_obj_t *parent, const char *hostname) {
    /* Container */
    lv_obj_t *cont = lv_obj_create(parent);
    lv_obj_set_size(cont, UNIT_W_N(2), UNIT_H);
    lv_obj_set_style_bg_color(cont, WS_COLOR_BG, 0);
    lv_obj_set_style_bg_opa(cont, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(cont, 1, 0);
    lv_obj_set_style_border_color(cont, WS_COLOR_SURFACE0, 0);
    lv_obj_set_style_radius(cont, 8, 0);
    lv_obj_set_style_pad_all(cont, CELL_PAD, 0);
    lv_obj_clear_flag(cont, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(cont, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(cont, 4, 0);

    /* ---- Header row (absolute-positioned labels) ---- */
    lv_obj_t *hdr = lv_obj_create(cont);
    lv_obj_set_size(hdr, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(hdr, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(hdr, 0, 0);
    lv_obj_set_style_pad_all(hdr, 0, 0);
    lv_obj_set_style_min_height(hdr, 60, 0);
    lv_obj_clear_flag(hdr, LV_OBJ_FLAG_SCROLLABLE);

    /* GPU column (left) */
    lv_obj_t *gpu_title = lv_label_create(hdr);
    lv_label_set_text(gpu_title, "GPU");
    lv_obj_set_style_text_color(gpu_title, WS_COLOR_FG, 0);
    lv_obj_set_style_text_font(gpu_title, &lv_font_montserrat_bold_20, 0);
    lv_obj_align(gpu_title, LV_ALIGN_TOP_LEFT, 0, 0);

    lv_obj_t *gpu_compute_lbl = lv_label_create(hdr);
    lv_label_set_text(gpu_compute_lbl, "Compute: ---%");
    lv_obj_set_style_text_color(gpu_compute_lbl, COLOR_GREEN, 0);
    lv_obj_set_style_text_font(gpu_compute_lbl, &lv_font_montserrat_16, 0);
    lv_obj_align(gpu_compute_lbl, LV_ALIGN_TOP_LEFT, 0, 24);

    lv_obj_t *gpu_vram_lbl = lv_label_create(hdr);
    lv_label_set_text(gpu_vram_lbl, "VRAM: ---- / ---- MB");
    lv_obj_set_style_text_color(gpu_vram_lbl, COLOR_ORANGE, 0);
    lv_obj_set_style_text_font(gpu_vram_lbl, &lv_font_montserrat_16, 0);
    lv_obj_align(gpu_vram_lbl, LV_ALIGN_TOP_LEFT, 0, 44);

    /* Hostname (center) */
    lv_obj_t *host_lbl = lv_label_create(hdr);
    lv_label_set_text(host_lbl, hostname ? hostname : "---");
    lv_obj_set_style_text_color(host_lbl, COLOR_ORANGE, 0);
    lv_obj_set_style_text_font(host_lbl, &lv_font_montserrat_bold_24, 0);
    lv_obj_align(host_lbl, LV_ALIGN_TOP_MID, 0, 0);

    /* System column (right) */
    lv_obj_t *sys_title = lv_label_create(hdr);
    lv_label_set_text(sys_title, "System");
    lv_obj_set_style_text_color(sys_title, WS_COLOR_FG, 0);
    lv_obj_set_style_text_font(sys_title, &lv_font_montserrat_bold_20, 0);
    lv_obj_align(sys_title, LV_ALIGN_TOP_RIGHT, 0, 0);

    lv_obj_t *sys_cpu_lbl = lv_label_create(hdr);
    lv_label_set_text(sys_cpu_lbl, "CPU: ---% / ---%");
    lv_obj_set_style_text_color(sys_cpu_lbl, COLOR_BLUE, 0);
    lv_obj_set_style_text_font(sys_cpu_lbl, &lv_font_montserrat_16, 0);
    lv_obj_align(sys_cpu_lbl, LV_ALIGN_TOP_RIGHT, 0, 24);

    lv_obj_t *sys_ram_lbl = lv_label_create(hdr);
    lv_label_set_text(sys_ram_lbl, "RAM: ---- / ---- GB");
    lv_obj_set_style_text_color(sys_ram_lbl, COLOR_RED, 0);
    lv_obj_set_style_text_font(sys_ram_lbl, &lv_font_montserrat_16, 0);
    lv_obj_align(sys_ram_lbl, LV_ALIGN_TOP_RIGHT, 0, 44);

    /* ---- Chart ---- */
    lv_obj_t *chart = lv_chart_create(cont);
    lv_obj_set_width(chart, LV_PCT(100));
    lv_obj_set_flex_grow(chart, 1);
    lv_obj_set_style_bg_color(chart, WS_COLOR_BG, 0);
    lv_obj_set_style_bg_opa(chart, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(chart, 0, 0);
    lv_obj_set_style_line_color(chart, WS_COLOR_SURFACE0, LV_PART_MAIN);
    lv_obj_set_style_line_width(chart, 1, LV_PART_MAIN);
    lv_obj_clear_flag(chart, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_set_style_line_width(chart, 2, LV_PART_ITEMS);
    lv_obj_set_style_size(chart, 0, 0, LV_PART_INDICATOR);

    lv_chart_set_type(chart, LV_CHART_TYPE_LINE);
    lv_chart_set_update_mode(chart, LV_CHART_UPDATE_MODE_SHIFT);
    lv_chart_set_point_count(chart, POINT_COUNT);
    lv_chart_set_div_line_count(chart, 5, 0);

    /* Primary Y: percentage (0-100) */
    lv_chart_set_range(chart, LV_CHART_AXIS_PRIMARY_Y, 0, 100);
    /* Secondary Y: MB (dynamic, default 16 GB) */
    lv_chart_set_range(chart, LV_CHART_AXIS_SECONDARY_Y, 0, 16384);

    /* 5 series */
    lv_chart_series_t *gpu_compute_ser =
        lv_chart_add_series(chart, COLOR_GREEN, LV_CHART_AXIS_PRIMARY_Y);
    lv_chart_series_t *cpu_avg_ser =
        lv_chart_add_series(chart, COLOR_BLUE, LV_CHART_AXIS_PRIMARY_Y);
    lv_chart_series_t *cpu_top_ser =
        lv_chart_add_series(chart, COLOR_PURPLE, LV_CHART_AXIS_PRIMARY_Y);
    lv_chart_series_t *vram_ser =
        lv_chart_add_series(chart, COLOR_ORANGE, LV_CHART_AXIS_SECONDARY_Y);
    lv_chart_series_t *ram_ser = lv_chart_add_series(chart, COLOR_RED, LV_CHART_AXIS_SECONDARY_Y);

    lv_chart_set_all_value(chart, gpu_compute_ser, 0);
    lv_chart_set_all_value(chart, cpu_avg_ser, 0);
    lv_chart_set_all_value(chart, cpu_top_ser, 0);
    lv_chart_set_all_value(chart, vram_ser, 0);
    lv_chart_set_all_value(chart, ram_ser, 0);

    /* Private data */
    dev_graph_priv_t *priv = lv_malloc(sizeof(dev_graph_priv_t));
    priv->gpu_compute_ser = gpu_compute_ser;
    priv->cpu_avg_ser = cpu_avg_ser;
    priv->cpu_top_ser = cpu_top_ser;
    priv->vram_ser = vram_ser;
    priv->ram_ser = ram_ser;
    priv->gpu_title = gpu_title;
    priv->gpu_compute_lbl = gpu_compute_lbl;
    priv->gpu_vram_lbl = gpu_vram_lbl;
    priv->host_lbl = host_lbl;
    priv->sys_title = sys_title;
    priv->sys_cpu_lbl = sys_cpu_lbl;
    priv->sys_ram_lbl = sys_ram_lbl;
    priv->vram_max = 16384;
    priv->ram_max = 131072; /* ~128 GB default */
    lv_obj_set_user_data(cont, priv);

    return cont;
}

static lv_obj_t *find_chart(lv_obj_t *graph) {
    uint32_t cnt = lv_obj_get_child_count(graph);
    for (uint32_t i = 0; i < cnt; i++) {
        lv_obj_t *child = lv_obj_get_child(graph, i);
        if (lv_obj_check_type(child, &lv_chart_class))
            return child;
    }
    return NULL;
}

static int32_t clamp_pct(float v) {
    int32_t r = (int32_t)(v + 0.5f);
    if (r < 0)
        r = 0;
    if (r > 100)
        r = 100;
    return r;
}

void dev_graph_update(lv_obj_t *graph, const dev_graph_data_t *data) {
    if (!graph || !data)
        return;
    dev_graph_priv_t *priv = lv_obj_get_user_data(graph);
    if (!priv)
        return;
    lv_obj_t *chart = find_chart(graph);
    if (!chart)
        return;

    /* Update secondary Y-axis range for the larger of VRAM/RAM */
    uint32_t mb_max = data->gpu_vram_total_mb;
    if (data->ram_total_mb > mb_max)
        mb_max = data->ram_total_mb;
    if (mb_max > 0 && (mb_max != priv->vram_max || mb_max != priv->ram_max)) {
        priv->vram_max = mb_max;
        priv->ram_max = mb_max;
        lv_chart_set_range(chart, LV_CHART_AXIS_SECONDARY_Y, 0, (int32_t)mb_max);
    }

    /* Push data points */
    lv_chart_set_next_value(chart, priv->gpu_compute_ser, clamp_pct(data->gpu_compute_pct));
    lv_chart_set_next_value(chart, priv->cpu_avg_ser, clamp_pct(data->cpu_pct));
    lv_chart_set_next_value(chart, priv->cpu_top_ser, clamp_pct(data->top_core_pct));
    lv_chart_set_next_value(chart, priv->vram_ser, (int32_t)data->gpu_vram_used_mb);
    lv_chart_set_next_value(chart, priv->ram_ser, (int32_t)data->ram_used_mb);

    /* Update GPU labels */
    char buf[64];
    snprintf(buf, sizeof(buf), "Compute: %d%%", clamp_pct(data->gpu_compute_pct));
    lv_label_set_text(priv->gpu_compute_lbl, buf);

    snprintf(buf, sizeof(buf), "VRAM: %u / %u MB", data->gpu_vram_used_mb, data->gpu_vram_total_mb);
    lv_label_set_text(priv->gpu_vram_lbl, buf);

    /* Update System labels */
    snprintf(buf, sizeof(buf), "CPU: %.0f%% / %.0f%%", data->cpu_pct, data->top_core_pct);
    lv_label_set_text(priv->sys_cpu_lbl, buf);

    float ram_used_gb = (float)data->ram_used_mb / 1024.0f;
    float ram_total_gb = (float)data->ram_total_mb / 1024.0f;
    snprintf(buf, sizeof(buf), "RAM: %.1f / %.1f GB", ram_used_gb, ram_total_gb);
    lv_label_set_text(priv->sys_ram_lbl, buf);

    lv_chart_refresh(chart);
}

void dev_graph_destroy(lv_obj_t *graph) {
    if (!graph)
        return;
    dev_graph_priv_t *priv = lv_obj_get_user_data(graph);
    if (priv)
        lv_free(priv);
    lv_obj_delete(graph);
}
