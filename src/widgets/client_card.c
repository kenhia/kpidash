#include "client_card.h"
#include "protocol.h"
#include <stdio.h>
#include <stdlib.h>
#include <math.h>

/* ---- Colors ---- */
#define COLOR_ONLINE     lv_color_hex(0x00C853)   /* green */
#define COLOR_OFFLINE    lv_color_hex(0xFF1744)   /* red   */
#define COLOR_BG         lv_color_hex(0x1E1E2E)
#define COLOR_FG         lv_color_hex(0xCDD6F4)
#define COLOR_MUTED      lv_color_hex(0x6C7086)
#define COLOR_ARC_BG     lv_color_hex(0x313244)
#define COLOR_CPU_AVG    lv_color_hex(0x89B4FA)   /* blue */
#define COLOR_CPU_TOP    lv_color_hex(0xCBA6F7)   /* purple */
#define COLOR_RAM        lv_color_hex(0xA6E3A1)   /* green */
#define COLOR_GPU_USAGE  lv_color_hex(0xF38BA8)   /* pink */
#define COLOR_GPU_VRAM   lv_color_hex(0xFAB387)   /* orange */
#define COLOR_DISK_OK    lv_color_hex(0xA6E3A1)   /* green  ≤60% */
#define COLOR_DISK_WARN  lv_color_hex(0xFAB387)   /* orange 60-75% */
#define COLOR_DISK_CRIT  lv_color_hex(0xF38BA8)   /* red    >75% */
#define COLOR_BAR_BG     lv_color_hex(0x585B70)
#define COLOR_GRAY       lv_color_hex(0x585B70)   /* disabled arc */
#define COLOR_DISK_TEXT  lv_color_hex(0x000000)   /* black text on light bar */
#define COLOR_DISK_BG    lv_color_hex(0xBAC2DE)   /* lighter gray bar bg */

/* ---- Layout constants (R2 → refined Phase 3.1) ---- */
#define CARD_WIDTH        624
#define ARC_AREA_SIZE     140
#define ARC_OUTER_WIDTH   21
#define ARC_INNER_WIDTH   21
#define ARC_GAP           2
#define HEALTH_CIRCLE_SZ  20
#define INNER_ARC_SIZE    (ARC_AREA_SIZE - 2 * (ARC_OUTER_WIDTH + ARC_GAP))
#define TEXT_BOX_WIDTH    220
#define DISK_BAR_GAP      5
#define DISK_BAR_MAX_H    120
#define MAX_DISPLAY_DISKS 6

/* Arc angle ranges — full 180° per side, trimmed 5° at each join */
#define LEFT_ARC_START    95
#define LEFT_ARC_END      265
#define RIGHT_ARC_START   275
#define RIGHT_ARC_END     85

/* ---- Per-card widget handles ---- */
typedef struct {
    lv_obj_t *health_circle;
    lv_obj_t *gpu_vram_arc;   /* outer left  */
    lv_obj_t *gpu_usage_arc;  /* inner left  */
    lv_obj_t *ram_arc;        /* outer right */
    lv_obj_t *cpu_top_arc;    /* inner right — purple, lower z */
    lv_obj_t *cpu_avg_arc;    /* inner right — blue, upper z   */
    lv_obj_t *gpu_vram_lbl;   /* "431 MB / 16.0 GB" */
    lv_obj_t *gpu_usage_lbl;  /* "0%" */
    lv_obj_t *ram_lbl;        /* "6.0 GB / 16.0 GB" */
    lv_obj_t *cpu_lbl;        /* "45% / 78%" */
    lv_obj_t *hostname_lbl;
    lv_obj_t *os_name_lbl;
    lv_obj_t *uptime_lbl;
    lv_obj_t *disk_cont;
} card_handles_t;

/* ---- Helpers ---- */

static void format_uptime(float seconds, char *buf, size_t len) {
    if (seconds <= 0.0f) { snprintf(buf, len, "--"); return; }
    long s = (long)seconds;
    int d = (int)(s / 86400);
    int h = (int)((s % 86400) / 3600);
    int m = (int)((s % 3600) / 60);
    if (d > 0)      snprintf(buf, len, "up %dd %dh", d, h);
    else if (h > 0) snprintf(buf, len, "up %dh %dm", h, m);
    else             snprintf(buf, len, "up %dm", m);
}

static void format_bytes(uint32_t mb, char *buf, size_t len) {
    if (mb >= 1024) snprintf(buf, len, "%.1f GB", mb / 1024.0f);
    else            snprintf(buf, len, "%u MB", mb);
}

/* Format GB float for disk bars: "1.8 TB", "237 GB", "28 GB" */
static void format_bytes_short(float gb, char *buf, size_t len) {
    if (gb >= 1024.0f)     snprintf(buf, len, "%.1f TB", gb / 1024.0f);
    else if (gb >= 100.0f) snprintf(buf, len, "%.0f GB", gb);
    else                   snprintf(buf, len, "%.1f GB", gb);
}

static int clamp_pct(float val) {
    if (val < 0.0f) return 0;
    if (val > 100.0f) return 100;
    return (int)(val + 0.5f);
}

static lv_obj_t *make_label(lv_obj_t *parent, const char *text,
                             lv_color_t color, const lv_font_t *font) {
    lv_obj_t *lbl = lv_label_create(parent);
    lv_label_set_text(lbl, text);
    lv_obj_set_style_text_color(lbl, color, 0);
    if (font) lv_obj_set_style_text_font(lbl, font, 0);
    lv_obj_clear_flag(lbl, LV_OBJ_FLAG_SCROLLABLE);
    return lbl;
}

/* Create a display-only arc centred in parent */
static lv_obj_t *make_arc(lv_obj_t *parent, int32_t size, int32_t width,
                           lv_value_precise_t bg_start, lv_value_precise_t bg_end,
                           lv_color_t indicator_color) {
    lv_obj_t *arc = lv_arc_create(parent);
    lv_obj_set_size(arc, size, size);
    lv_obj_center(arc);
    lv_obj_clear_flag(arc, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(arc, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(arc, 0, 0);
    lv_obj_set_style_pad_all(arc, 0, 0);

    /* Hide knob */
    lv_obj_set_style_bg_opa(arc, LV_OPA_TRANSP, LV_PART_KNOB);
    lv_obj_set_style_pad_all(arc, 0, LV_PART_KNOB);

    /* Background arc — flat ends */
    lv_obj_set_style_arc_width(arc, width, LV_PART_MAIN);
    lv_obj_set_style_arc_color(arc, COLOR_ARC_BG, LV_PART_MAIN);
    lv_obj_set_style_arc_rounded(arc, false, LV_PART_MAIN);

    /* Indicator arc — flat ends */
    lv_obj_set_style_arc_width(arc, width, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(arc, indicator_color, LV_PART_INDICATOR);
    lv_obj_set_style_arc_rounded(arc, false, LV_PART_INDICATOR);

    lv_arc_set_bg_angles(arc, bg_start, bg_end);
    lv_arc_set_range(arc, 0, 100);
    lv_arc_set_value(arc, 0);
    lv_arc_set_mode(arc, LV_ARC_MODE_NORMAL);

    return arc;
}

static lv_color_t disk_bar_color(float pct) {
    if (pct > 75.0f) return COLOR_DISK_CRIT;
    if (pct > 60.0f) return COLOR_DISK_WARN;
    return COLOR_DISK_OK;
}

/* Free handle struct when card is deleted */
static void card_delete_cb(lv_event_t *e) {
    lv_obj_t *card = lv_event_get_target(e);
    card_handles_t *h = lv_obj_get_user_data(card);
    free(h);
}

/* ---- Widget creation (Phase 3.1) ---- */

lv_obj_t *client_card_create(lv_obj_t *parent, const char *hostname) {
    /* Card container */
    lv_obj_t *card = lv_obj_create(parent);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(card, COLOR_BG, 0);
    lv_obj_set_style_radius(card, 8, 0);
    lv_obj_set_style_border_width(card, 1, 0);
    lv_obj_set_style_border_color(card, lv_color_hex(0x45475A), 0);
    lv_obj_set_style_pad_all(card, 8, 0);
    lv_obj_set_style_pad_row(card, 4, 0);
    lv_obj_set_width(card, CARD_WIDTH);
    lv_obj_set_height(card, 620);
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(card, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                           LV_FLEX_ALIGN_START);

    card_handles_t *h = calloc(1, sizeof(card_handles_t));
    lv_obj_set_user_data(card, h);
    lv_obj_add_event_cb(card, card_delete_cb, LV_EVENT_DELETE, NULL);

    /* ======== Arc gauge row ======== */
    lv_obj_t *arc_row = lv_obj_create(card);
    lv_obj_clear_flag(arc_row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(arc_row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(arc_row, 0, 0);
    lv_obj_set_style_pad_all(arc_row, 0, 0);
    lv_obj_set_size(arc_row, LV_PCT(100), ARC_AREA_SIZE);

    /* ----- GPU text (left side, two separate labels with spacing) ----- */
    lv_obj_t *gpu_text_cont = lv_obj_create(arc_row);
    lv_obj_clear_flag(gpu_text_cont, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(gpu_text_cont, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(gpu_text_cont, 0, 0);
    lv_obj_set_style_pad_all(gpu_text_cont, 0, 0);
    lv_obj_set_style_pad_row(gpu_text_cont, 8, 0);
    lv_obj_set_size(gpu_text_cont, TEXT_BOX_WIDTH, ARC_AREA_SIZE);
    lv_obj_set_flex_flow(gpu_text_cont, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(gpu_text_cont, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_END,
                           LV_FLEX_ALIGN_END);
    lv_obj_align(gpu_text_cont, LV_ALIGN_LEFT_MID, 2, 0);

    h->gpu_vram_lbl = make_label(gpu_text_cont, "VRAM\n--", COLOR_GPU_VRAM,
                                  &lv_font_montserrat_14);
    lv_obj_set_style_text_align(h->gpu_vram_lbl, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_set_width(h->gpu_vram_lbl, TEXT_BOX_WIDTH);

    h->gpu_usage_lbl = make_label(gpu_text_cont, "Usage\n--", COLOR_GPU_USAGE,
                                   &lv_font_montserrat_14);
    lv_obj_set_style_text_align(h->gpu_usage_lbl, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_set_width(h->gpu_usage_lbl, TEXT_BOX_WIDTH);

    /* ----- Arc area container ----- */
    lv_obj_t *arc_area = lv_obj_create(arc_row);
    lv_obj_clear_flag(arc_area, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(arc_area, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(arc_area, 0, 0);
    lv_obj_set_style_pad_all(arc_area, 0, 0);
    lv_obj_set_size(arc_area, ARC_AREA_SIZE, ARC_AREA_SIZE);
    lv_obj_center(arc_area);

    /* Outer arcs — GPU VRAM left, RAM right */
    h->gpu_vram_arc = make_arc(arc_area, ARC_AREA_SIZE, ARC_OUTER_WIDTH,
                                LEFT_ARC_START, LEFT_ARC_END, COLOR_GPU_VRAM);
    h->ram_arc = make_arc(arc_area, ARC_AREA_SIZE, ARC_OUTER_WIDTH,
                           RIGHT_ARC_START, RIGHT_ARC_END, COLOR_RAM);

    /* Inner arcs — GPU usage left, CPU top + avg right */
    h->gpu_usage_arc = make_arc(arc_area, INNER_ARC_SIZE, ARC_INNER_WIDTH,
                                 LEFT_ARC_START, LEFT_ARC_END, COLOR_GPU_USAGE);
    h->cpu_top_arc = make_arc(arc_area, INNER_ARC_SIZE, ARC_INNER_WIDTH,
                               RIGHT_ARC_START, RIGHT_ARC_END, COLOR_CPU_TOP);
    h->cpu_avg_arc = make_arc(arc_area, INNER_ARC_SIZE, ARC_INNER_WIDTH,
                               RIGHT_ARC_START, RIGHT_ARC_END, COLOR_CPU_AVG);

    /* Center health circle */
    h->health_circle = lv_obj_create(arc_area);
    lv_obj_clear_flag(h->health_circle, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(h->health_circle, HEALTH_CIRCLE_SZ, HEALTH_CIRCLE_SZ);
    lv_obj_set_style_radius(h->health_circle, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(h->health_circle, 0, 0);
    lv_obj_set_style_bg_color(h->health_circle, COLOR_OFFLINE, 0);
    lv_obj_center(h->health_circle);

    /* ----- CPU/RAM text (right side, two separate labels with spacing) ----- */
    lv_obj_t *cpuram_cont = lv_obj_create(arc_row);
    lv_obj_clear_flag(cpuram_cont, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(cpuram_cont, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(cpuram_cont, 0, 0);
    lv_obj_set_style_pad_all(cpuram_cont, 0, 0);
    lv_obj_set_style_pad_row(cpuram_cont, 8, 0);
    lv_obj_set_size(cpuram_cont, TEXT_BOX_WIDTH, ARC_AREA_SIZE);
    lv_obj_set_flex_flow(cpuram_cont, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(cpuram_cont, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START,
                           LV_FLEX_ALIGN_START);
    lv_obj_align(cpuram_cont, LV_ALIGN_RIGHT_MID, -2, 0);

    h->ram_lbl = make_label(cpuram_cont, "RAM\n--", COLOR_RAM, &lv_font_montserrat_14);
    lv_obj_set_style_text_align(h->ram_lbl, LV_TEXT_ALIGN_LEFT, 0);
    lv_obj_set_width(h->ram_lbl, TEXT_BOX_WIDTH);

    h->cpu_lbl = make_label(cpuram_cont, "CPU\n--", COLOR_CPU_AVG, &lv_font_montserrat_14);
    lv_obj_set_style_text_align(h->cpu_lbl, LV_TEXT_ALIGN_LEFT, 0);
    lv_obj_set_width(h->cpu_lbl, TEXT_BOX_WIDTH);

    /* ======== Uptime (right-justified, above hostname) ======== */
    h->uptime_lbl = make_label(card, "--", COLOR_MUTED, &lv_font_montserrat_14);
    lv_obj_set_width(h->uptime_lbl, LV_PCT(100));
    lv_obj_set_style_text_align(h->uptime_lbl, LV_TEXT_ALIGN_RIGHT, 0);

    /* ======== Hostname (centered, large font) ======== */
    h->hostname_lbl = lv_label_create(card);
    lv_label_set_text(h->hostname_lbl, hostname);
    lv_obj_set_style_text_color(h->hostname_lbl, COLOR_GPU_VRAM, 0);
    lv_obj_set_style_text_font(h->hostname_lbl, &lv_font_montserrat_20, 0);
    lv_label_set_long_mode(h->hostname_lbl, LV_LABEL_LONG_DOT);
    lv_obj_set_width(h->hostname_lbl, LV_PCT(100));
    lv_obj_set_style_text_align(h->hostname_lbl, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_clear_flag(h->hostname_lbl, LV_OBJ_FLAG_SCROLLABLE);

    /* ======== OS name (centered, smaller) ======== */
    h->os_name_lbl = make_label(card, "", COLOR_MUTED, &lv_font_montserrat_14);
    lv_label_set_long_mode(h->os_name_lbl, LV_LABEL_LONG_DOT);
    lv_obj_set_width(h->os_name_lbl, LV_PCT(100));
    lv_obj_set_style_text_align(h->os_name_lbl, LV_TEXT_ALIGN_CENTER, 0);

    /* ======== Disk bars container ======== */
    h->disk_cont = lv_obj_create(card);
    lv_obj_clear_flag(h->disk_cont, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(h->disk_cont, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(h->disk_cont, 0, 0);
    lv_obj_set_style_pad_all(h->disk_cont, 0, 0);
    lv_obj_set_style_pad_row(h->disk_cont, DISK_BAR_GAP, 0);
    lv_obj_set_width(h->disk_cont, LV_PCT(100));
    lv_obj_set_height(h->disk_cont, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(h->disk_cont, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_grow(h->disk_cont, 1);

    return card;
}

/* ---- Health update (T022) ---- */

void client_card_update_health(lv_obj_t *card, const client_info_t *c) {
    card_handles_t *h = lv_obj_get_user_data(card);
    if (!h) return;

    /* Center circle color */
    lv_obj_set_style_bg_color(h->health_circle,
                               c->online ? COLOR_ONLINE : COLOR_OFFLINE, 0);

    /* Uptime */
    char buf[32];
    if (c->online)
        format_uptime(c->uptime_seconds, buf, sizeof(buf));
    else
        snprintf(buf, sizeof(buf), "offline");
    lv_label_set_text(h->uptime_lbl, buf);

    /* OS name */
    if (c->os_name[0] != '\0')
        lv_label_set_text(h->os_name_lbl, c->os_name);
}

/* ---- Telemetry update (Phase 3.1) ---- */

static void update_disk_bars(card_handles_t *h, const client_info_t *c) {
    lv_obj_clean(h->disk_cont);
    int count = c->disk_count > MAX_DISPLAY_DISKS ? MAX_DISPLAY_DISKS : c->disk_count;
    if (count == 0) return;

    /* Each disk bar: lv_bar with overlaid text labels */
    /* Bar height: flex-grow fills remaining card space; use 30px as baseline */
    int32_t bar_h = 30;

    for (int i = 0; i < count; i++) {
        const disk_entry_t *d = &c->disks[i];
        char buf[32];

        /* Container for one disk row (bar + overlaid labels) */
        lv_obj_t *row = lv_obj_create(h->disk_cont);
        lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(row, 0, 0);
        lv_obj_set_style_pad_all(row, 0, 0);
        lv_obj_set_width(row, LV_PCT(100));
        lv_obj_set_height(row, bar_h);
        lv_obj_set_style_max_height(row, DISK_BAR_MAX_H, 0);
        lv_obj_set_flex_grow(row, 1);

        /* Bar (fills entire row) */
        lv_obj_t *bar = lv_bar_create(row);
        lv_obj_set_size(bar, LV_PCT(100), LV_PCT(100));
        lv_obj_set_pos(bar, 0, 0);
        lv_obj_set_style_bg_color(bar, COLOR_DISK_BG, LV_PART_MAIN);
        lv_obj_set_style_bg_color(bar, disk_bar_color(d->pct), LV_PART_INDICATOR);
        lv_obj_set_style_radius(bar, 4, LV_PART_MAIN);
        lv_obj_set_style_radius(bar, 4, LV_PART_INDICATOR);
        lv_bar_set_range(bar, 0, 100);
        lv_bar_set_value(bar, clamp_pct(d->pct), LV_ANIM_OFF);
        lv_obj_clear_flag(bar, LV_OBJ_FLAG_SCROLLABLE);

        /* Used amount — left justified inside bar */
        format_bytes_short(d->used_gb, buf, sizeof(buf));
        lv_obj_t *used_lbl = lv_label_create(row);
        lv_label_set_text(used_lbl, buf);
        lv_obj_set_style_text_color(used_lbl, COLOR_DISK_TEXT, 0);
        lv_obj_set_style_text_font(used_lbl, &lv_font_montserrat_14, 0);
        lv_obj_clear_flag(used_lbl, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_align(used_lbl, LV_ALIGN_LEFT_MID, 6, 0);

        /* Drive label — centered inside bar */
        lv_obj_t *lbl = lv_label_create(row);
        lv_label_set_text(lbl, d->label);
        lv_obj_set_style_text_color(lbl, COLOR_DISK_TEXT, 0);
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_16, 0);
        lv_obj_clear_flag(lbl, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_align(lbl, LV_ALIGN_CENTER, 0, 0);

        /* Total amount — right justified inside bar */
        format_bytes_short(d->total_gb, buf, sizeof(buf));
        lv_obj_t *total_lbl = lv_label_create(row);
        lv_label_set_text(total_lbl, buf);
        lv_obj_set_style_text_color(total_lbl, COLOR_DISK_TEXT, 0);
        lv_obj_set_style_text_font(total_lbl, &lv_font_montserrat_14, 0);
        lv_obj_clear_flag(total_lbl, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_align(total_lbl, LV_ALIGN_RIGHT_MID, -6, 0);
    }
}

void client_card_update_telemetry(lv_obj_t *card, const client_info_t *c) {
    card_handles_t *h = lv_obj_get_user_data(card);
    if (!h) return;

    char buf[64];

    /* ---- GPU arcs + text ---- */
    if (c->gpu.present) {
        /* VRAM arc: % of total */
        lv_arc_set_value(h->gpu_vram_arc,
            c->gpu.vram_total_mb > 0
            ? clamp_pct(100.0f * c->gpu.vram_used_mb / c->gpu.vram_total_mb)
            : 0);
        lv_arc_set_value(h->gpu_usage_arc, clamp_pct(c->gpu.compute_pct));

        /* Restore indicator colors */
        lv_obj_set_style_arc_color(h->gpu_vram_arc, COLOR_GPU_VRAM, LV_PART_INDICATOR);
        lv_obj_set_style_arc_color(h->gpu_usage_arc, COLOR_GPU_USAGE, LV_PART_INDICATOR);
        lv_obj_set_style_arc_color(h->gpu_vram_arc, COLOR_ARC_BG, LV_PART_MAIN);
        lv_obj_set_style_arc_color(h->gpu_usage_arc, COLOR_ARC_BG, LV_PART_MAIN);

        /* GPU VRAM text */
        char vu[16], vt[16];
        format_bytes(c->gpu.vram_used_mb, vu, sizeof(vu));
        format_bytes(c->gpu.vram_total_mb, vt, sizeof(vt));
        snprintf(buf, sizeof(buf), "%s / %s", vu, vt);
        lv_label_set_text(h->gpu_vram_lbl, buf);

        /* GPU usage text */
        snprintf(buf, sizeof(buf), "%.0f%%", c->gpu.compute_pct);
        lv_label_set_text(h->gpu_usage_lbl, buf);

        lv_obj_clear_flag(h->gpu_vram_lbl, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(h->gpu_usage_lbl, LV_OBJ_FLAG_HIDDEN);
    } else {
        /* No-GPU fallback — solid gray arcs, hide text */
        lv_arc_set_value(h->gpu_vram_arc, 0);
        lv_arc_set_value(h->gpu_usage_arc, 0);
        lv_obj_set_style_arc_color(h->gpu_vram_arc, COLOR_GRAY, LV_PART_MAIN);
        lv_obj_set_style_arc_color(h->gpu_vram_arc, COLOR_GRAY, LV_PART_INDICATOR);
        lv_obj_set_style_arc_color(h->gpu_usage_arc, COLOR_GRAY, LV_PART_MAIN);
        lv_obj_set_style_arc_color(h->gpu_usage_arc, COLOR_GRAY, LV_PART_INDICATOR);
        lv_obj_add_flag(h->gpu_vram_lbl, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(h->gpu_usage_lbl, LV_OBJ_FLAG_HIDDEN);
    }

    /* ---- RAM arc + text ---- */
    lv_arc_set_value(h->ram_arc,
        c->ram_total_mb > 0
        ? clamp_pct(100.0f * c->ram_used_mb / c->ram_total_mb)
        : 0);
    char ru[16], rt[16];
    format_bytes(c->ram_used_mb, ru, sizeof(ru));
    format_bytes(c->ram_total_mb, rt, sizeof(rt));
    snprintf(buf, sizeof(buf), "%s / %s", ru, rt);
    lv_label_set_text(h->ram_lbl, buf);

    /* ---- CPU arcs + text ---- */
    lv_arc_set_value(h->cpu_top_arc, clamp_pct(c->top_core_pct));
    lv_arc_set_value(h->cpu_avg_arc, clamp_pct(c->cpu_pct));
    snprintf(buf, sizeof(buf), "%.0f%% / %.0f%%", c->cpu_pct, c->top_core_pct);
    lv_label_set_text(h->cpu_lbl, buf);

    /* ---- Disk bars ---- */
    update_disk_bars(h, c);
}
