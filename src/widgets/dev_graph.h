#ifndef WIDGETS_DEV_GRAPH_H
#define WIDGETS_DEV_GRAPH_H

#include <stdint.h>

#include "lvgl.h"

/**
 * Dev graph widget: single lv_chart with 5 line series on two Y-axes.
 *
 * Primary Y-axis (0-100%):
 *   - GPU compute %  (green)
 *   - CPU average %  (blue)
 *   - CPU top core % (purple)
 *
 * Secondary Y-axis (0-max MB, dynamic):
 *   - VRAM used MB   (orange)
 *   - RAM used MB    (teal)
 *
 * 300 data points = 5 min at 1s dev telemetry interval.
 * Sized to occupy two client card slots (1256 x 620).
 */

typedef struct {
    float cpu_pct;
    float top_core_pct;
    uint32_t ram_used_mb;
    uint32_t ram_total_mb;
    float gpu_compute_pct;
    uint32_t gpu_vram_used_mb;
    uint32_t gpu_vram_total_mb;
} dev_graph_data_t;

lv_obj_t *dev_graph_create(lv_obj_t *parent, const char *hostname);
void dev_graph_update(lv_obj_t *graph, const dev_graph_data_t *data);
void dev_graph_destroy(lv_obj_t *graph);

#endif /* WIDGETS_DEV_GRAPH_H */
