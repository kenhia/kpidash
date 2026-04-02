#include "client_card.h"
#include "protocol.h"
#include <stdio.h>
#include <math.h>

#define COLOR_ONLINE  lv_color_hex(0x00C853)   /* green */
#define COLOR_OFFLINE lv_color_hex(0xFF1744)   /* red   */
#define COLOR_BG      lv_color_hex(0x1E1E2E)
#define COLOR_FG      lv_color_hex(0xCDD6F4)
#define COLOR_MUTED   lv_color_hex(0x6C7086)
#define COLOR_BAR_BG  lv_color_hex(0x313244)
#define COLOR_CPU     lv_color_hex(0x89DCEB)
#define COLOR_RAM     lv_color_hex(0xA6E3A1)
#define COLOR_GPU     lv_color_hex(0xF38BA8)
#define COLOR_DISK    lv_color_hex(0xFAB387)

/* Tag keys for finding child widgets by type */
#define TAG_LED      1
#define TAG_UPTIME   2
#define TAG_CPU      3
#define TAG_RAM      4
#define TAG_GPU_SECT 5
#define TAG_DISK_CONT 6

/* ---- Helpers ---- */

static lv_obj_t *make_label(lv_obj_t *parent, const char *text, lv_color_t color,
                              const lv_font_t *font) {
    lv_obj_t *lbl = lv_label_create(parent);
    lv_label_set_text(lbl, text);
    lv_obj_set_style_text_color(lbl, color, 0);
    if (font) lv_obj_set_style_text_font(lbl, font, 0);
    lv_obj_clear_flag(lbl, LV_OBJ_FLAG_SCROLLABLE);
    return lbl;
}

static lv_obj_t *find_by_tag(lv_obj_t *parent, int tag) {
    uint32_t n = lv_obj_get_child_count(parent);
    for (uint32_t i = 0; i < n; i++) {
        lv_obj_t *ch = lv_obj_get_child(parent, i);
        if ((int)(intptr_t)lv_obj_get_user_data(ch) == tag) return ch;
        lv_obj_t *found = find_by_tag(ch, tag);
        if (found) return found;
    }
    return NULL;
}

static void format_uptime(float seconds, char *buf, size_t len) {
    if (seconds <= 0.0f) {
        snprintf(buf, len, "--");
        return;
    }
    long s = (long)seconds;
    int d = (int)(s / 86400);
    int h = (int)((s % 86400) / 3600);
    int m = (int)((s % 3600) / 60);
    if (d > 0)
        snprintf(buf, len, "up %dd %dh", d, h);
    else if (h > 0)
        snprintf(buf, len, "up %dh %dm", h, m);
    else
        snprintf(buf, len, "up %dm", m);
}

static void format_bytes(uint32_t mb, char *buf, size_t len) {
    if (mb >= 1024)
        snprintf(buf, len, "%.1f GB", mb / 1024.0f);
    else
        snprintf(buf, len, "%u MB", mb);
}

static const char *disk_type_str(disk_type_t t) {
    switch (t) {
        case DISK_NVME:  return "NVMe";
        case DISK_SSD:   return "SSD";
        case DISK_HDD:   return "HDD";
        default:         return "Other";
    }
}

/* ---- Widget creation ---- */

lv_obj_t *client_card_create(lv_obj_t *parent, const char *hostname) {
    /* Card container */
    lv_obj_t *card = lv_obj_create(parent);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(card, COLOR_BG, 0);
    lv_obj_set_style_radius(card, 8, 0);
    lv_obj_set_style_border_width(card, 1, 0);
    lv_obj_set_style_border_color(card, lv_color_hex(0x45475A), 0);
    lv_obj_set_style_pad_all(card, 12, 0);
    lv_obj_set_style_pad_row(card, 6, 0);
    lv_obj_set_width(card, LV_PCT(100));
    lv_obj_set_height(card, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(card, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START,
                           LV_FLEX_ALIGN_START);

    /* Header row: LED + hostname + uptime */
    lv_obj_t *header = lv_obj_create(card);
    lv_obj_clear_flag(header, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(header, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(header, 0, 0);
    lv_obj_set_style_pad_all(header, 0, 0);
    lv_obj_set_width(header, LV_PCT(100));
    lv_obj_set_height(header, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(header, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(header, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                           LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_column(header, 8, 0);

    /* Health LED */
    lv_obj_t *led = lv_obj_create(header);
    lv_obj_clear_flag(led, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(led, 14, 14);
    lv_obj_set_style_radius(led, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(led, 0, 0);
    lv_obj_set_style_bg_color(led, COLOR_OFFLINE, 0);
    lv_obj_set_user_data(led, (void *)(intptr_t)TAG_LED);

    /* Hostname */
    make_label(header, hostname, COLOR_FG, &lv_font_montserrat_20);

    /* Uptime (right-aligned flex spacer) */
    lv_obj_t *spacer = lv_obj_create(header);
    lv_obj_clear_flag(spacer, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(spacer, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(spacer, 0, 0);
    lv_obj_set_style_pad_all(spacer, 0, 0);
    lv_obj_set_flex_grow(spacer, 1);

    lv_obj_t *uptime_lbl = make_label(header, "--", COLOR_MUTED, &lv_font_montserrat_14);
    lv_obj_set_user_data(uptime_lbl, (void *)(intptr_t)TAG_UPTIME);

    /* Separator */
    lv_obj_t *sep = lv_obj_create(card);
    lv_obj_clear_flag(sep, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_width(sep, LV_PCT(100));
    lv_obj_set_height(sep, 1);
    lv_obj_set_style_bg_color(sep, lv_color_hex(0x45475A), 0);
    lv_obj_set_style_border_width(sep, 0, 0);
    lv_obj_set_style_pad_all(sep, 0, 0);

    /* CPU label */
    lv_obj_t *cpu_lbl = make_label(card, "CPU: --", COLOR_CPU, &lv_font_montserrat_14);
    lv_obj_set_user_data(cpu_lbl, (void *)(intptr_t)TAG_CPU);

    /* RAM label */
    lv_obj_t *ram_lbl = make_label(card, "RAM: --", COLOR_RAM, &lv_font_montserrat_14);
    lv_obj_set_user_data(ram_lbl, (void *)(intptr_t)TAG_RAM);

    /* GPU section (hidden initially) */
    lv_obj_t *gpu_sect = lv_obj_create(card);
    lv_obj_clear_flag(gpu_sect, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(gpu_sect, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(gpu_sect, 0, 0);
    lv_obj_set_style_pad_all(gpu_sect, 0, 0);
    lv_obj_set_style_pad_row(gpu_sect, 2, 0);
    lv_obj_set_width(gpu_sect, LV_PCT(100));
    lv_obj_set_height(gpu_sect, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(gpu_sect, LV_FLEX_FLOW_COLUMN);
    lv_obj_add_flag(gpu_sect, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_user_data(gpu_sect, (void *)(intptr_t)TAG_GPU_SECT);
    /* Pre-create GPU labels (updated in update_telemetry) */
    make_label(gpu_sect, "GPU: --", COLOR_GPU, &lv_font_montserrat_14);  /* name+usage */
    make_label(gpu_sect, "VRAM: --", COLOR_GPU, &lv_font_montserrat_14); /* vram */

    /* Disk container */
    lv_obj_t *disk_cont = lv_obj_create(card);
    lv_obj_clear_flag(disk_cont, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(disk_cont, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(disk_cont, 0, 0);
    lv_obj_set_style_pad_all(disk_cont, 0, 0);
    lv_obj_set_style_pad_row(disk_cont, 2, 0);
    lv_obj_set_width(disk_cont, LV_PCT(100));
    lv_obj_set_height(disk_cont, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(disk_cont, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_user_data(disk_cont, (void *)(intptr_t)TAG_DISK_CONT);

    return card;
}

void client_card_update_health(lv_obj_t *card, bool online, float uptime_s) {
    lv_obj_t *led = find_by_tag(card, TAG_LED);
    if (led) {
        lv_obj_set_style_bg_color(led, online ? COLOR_ONLINE : COLOR_OFFLINE, 0);
    }

    lv_obj_t *uptime_lbl = find_by_tag(card, TAG_UPTIME);
    if (uptime_lbl) {
        char buf[32];
        if (online)
            format_uptime(uptime_s, buf, sizeof(buf));
        else
            snprintf(buf, sizeof(buf), "offline");
        lv_label_set_text(uptime_lbl, buf);
    }
}

void client_card_update_telemetry(lv_obj_t *card, const client_info_t *c) {
    char buf[128];

    /* CPU */
    lv_obj_t *cpu_lbl = find_by_tag(card, TAG_CPU);
    if (cpu_lbl) {
        snprintf(buf, sizeof(buf), "CPU: %.1f%%  core: %.1f%%",
                 c->cpu_pct, c->top_core_pct);
        lv_label_set_text(cpu_lbl, buf);
    }

    /* RAM */
    lv_obj_t *ram_lbl = find_by_tag(card, TAG_RAM);
    if (ram_lbl) {
        char used[16], total[16];
        format_bytes(c->ram_used_mb, used, sizeof(used));
        format_bytes(c->ram_total_mb, total, sizeof(total));
        snprintf(buf, sizeof(buf), "RAM: %s / %s", used, total);
        lv_label_set_text(ram_lbl, buf);
    }

    /* GPU */
    lv_obj_t *gpu_sect = find_by_tag(card, TAG_GPU_SECT);
    if (gpu_sect) {
        if (c->gpu.present) {
            lv_obj_clear_flag(gpu_sect, LV_OBJ_FLAG_HIDDEN);
            lv_obj_t *gname = lv_obj_get_child(gpu_sect, 0);
            lv_obj_t *gvram = lv_obj_get_child(gpu_sect, 1);
            if (gname) {
                snprintf(buf, sizeof(buf), "GPU: %s  %.1f%%",
                         c->gpu.name, c->gpu.compute_pct);
                lv_label_set_text(gname, buf);
            }
            if (gvram) {
                char vu[16], vt[16];
                format_bytes(c->gpu.vram_used_mb, vu, sizeof(vu));
                format_bytes(c->gpu.vram_total_mb, vt, sizeof(vt));
                snprintf(buf, sizeof(buf), "VRAM: %s / %s", vu, vt);
                lv_label_set_text(gvram, buf);
            }
        } else {
            lv_obj_add_flag(gpu_sect, LV_OBJ_FLAG_HIDDEN);
        }
    }

    /* Disks — delete previous rows and recreate */
    lv_obj_t *disk_cont = find_by_tag(card, TAG_DISK_CONT);
    if (disk_cont) {
        lv_obj_clean(disk_cont);
        for (int i = 0; i < c->disk_count; i++) {
            const disk_entry_t *d = &c->disks[i];
            snprintf(buf, sizeof(buf), "%s [%s] %.1f GB / %.1f GB  %.0f%%",
                     d->label, disk_type_str(d->type),
                     d->used_gb, d->total_gb, d->pct);
            make_label(disk_cont, buf, COLOR_DISK, &lv_font_montserrat_14);
        }
    }
}
