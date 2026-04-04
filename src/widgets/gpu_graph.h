#ifndef WIDGETS_GPU_GRAPH_H
#define WIDGETS_GPU_GRAPH_H

#include "lvgl.h"
#include <stdint.h>

/**
 * Create a GPU graph widget: single lv_chart with two line series
 * (GPU compute % on primary Y-axis, VRAM used MB on secondary Y-axis).
 * Uses LV_CHART_UPDATE_MODE_SHIFT for scrolling time-series.
 * 60 data points (5 min at 5s telemetry interval).
 */
lv_obj_t *gpu_graph_create(lv_obj_t *parent);

/**
 * Push a new data point to both series.
 * vram_total_mb is used to set the secondary Y-axis range dynamically.
 */
void gpu_graph_update(lv_obj_t *graph, float compute_pct,
                      uint32_t vram_used_mb, uint32_t vram_total_mb);

/**
 * Destroy the graph widget. Safe to call with NULL.
 */
void gpu_graph_destroy(lv_obj_t *graph);

#endif /* WIDGETS_GPU_GRAPH_H */
