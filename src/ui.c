#include "ui.h"
#include "protocol.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

/* ── Styles ───────────────────────────────────────────────────────── */
static lv_style_t style_card;
static lv_style_t style_title_bar;

/* ── Top-level widgets ────────────────────────────────────────────── */
static lv_obj_t *title_label;
static lv_obj_t *clock_label;
static lv_obj_t *client_list;   /* scrollable container for cards */

/* ── Resource image paths (POSIX FS drive 'A') ────────────────────── */
#define IMG_NVME "A:/home/ken/src/kpidash/resources/nvme.png"
#define IMG_HDD  "A:/home/ken/src/kpidash/resources/hdd.png"
#define IMG_SSD  "A:/home/ken/src/kpidash/resources/ssd.png"

/* ── Helpers ──────────────────────────────────────────────────────── */

static void format_elapsed(time_t start, char *buf, size_t len) {
    if (start == 0) {
        snprintf(buf, len, "--:--:--");
        return;
    }
    time_t now = time(NULL);
    long elapsed = (long)(now - start);
    if (elapsed < 0) elapsed = 0;
    int h = (int)(elapsed / 3600);
    int m = (int)((elapsed % 3600) / 60);
    int s = (int)(elapsed % 60);
    snprintf(buf, len, "%02d:%02d:%02d", h, m, s);
}

static void update_clock(void) {
    time_t now = time(NULL);
    struct tm *tm = localtime(&now);
    char buf[16];
    snprintf(buf, sizeof(buf), "%02d:%02d:%02d", tm->tm_hour, tm->tm_min, tm->tm_sec);
    lv_label_set_text(clock_label, buf);
}

/* ── Image card creation ──────────────────────────────────────────── */

static lv_obj_t *create_image_card(lv_obj_t *parent, const char *img_path, const char *label_text) {
    lv_obj_t *card = lv_obj_create(parent);
    lv_obj_add_style(card, &style_card, 0);
    lv_obj_set_size(card, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_pad_all(card, 12, 0);
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(card, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(card, 8, 0);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *img = lv_image_create(card);
    lv_image_set_src(img, img_path);
    lv_obj_set_size(img, 64, 64);
    lv_image_set_inner_align(img, LV_IMAGE_ALIGN_CENTER);

    lv_obj_t *lbl = lv_label_create(card);
    lv_label_set_text(lbl, label_text);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(lbl, lv_color_white(), 0);

    return card;
}

/* ── Client card creation ─────────────────────────────────────────── */

static void create_client_card(client_info_t *c) {
    /* Card container */
    c->container = lv_obj_create(client_list);
    lv_obj_add_style(c->container, &style_card, 0);
    lv_obj_set_width(c->container, LV_PCT(100));
    lv_obj_set_height(c->container, LV_SIZE_CONTENT);
    lv_obj_set_style_pad_all(c->container, 16, 0);
    lv_obj_set_flex_flow(c->container, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(c->container, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(c->container, 20, 0);

    /* LED indicator */
    c->status_led = lv_led_create(c->container);
    lv_led_set_color(c->status_led, lv_palette_main(LV_PALETTE_GREEN));
    lv_obj_set_size(c->status_led, 32, 32);
    lv_led_on(c->status_led);

    /* Right-side column: hostname on top, task + elapsed below */
    lv_obj_t *col = lv_obj_create(c->container);
    lv_obj_remove_style_all(col);
    lv_obj_set_flex_grow(col, 1);
    lv_obj_set_height(col, LV_SIZE_CONTENT);
    lv_obj_set_width(col, LV_PCT(100));
    lv_obj_set_flex_flow(col, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(col, 6, 0);

    /* Row 1: hostname */
    c->hostname_label = lv_label_create(col);
    lv_label_set_text(c->hostname_label, c->hostname);
    lv_obj_set_style_text_font(c->hostname_label, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(c->hostname_label, lv_color_white(), 0);

    /* Row 2: task + elapsed in a row */
    lv_obj_t *task_row = lv_obj_create(col);
    lv_obj_remove_style_all(task_row);
    lv_obj_set_width(task_row, LV_PCT(100));
    lv_obj_set_height(task_row, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(task_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(task_row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    c->task_label = lv_label_create(task_row);
    lv_label_set_text(c->task_label, "(no task)");
    lv_obj_set_style_text_font(c->task_label, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(c->task_label, lv_palette_lighten(LV_PALETTE_GREY, 2), 0);
    lv_obj_set_flex_grow(c->task_label, 1);

    c->elapsed_label = lv_label_create(task_row);
    lv_label_set_text(c->elapsed_label, "--:--:--");
    lv_obj_set_style_text_font(c->elapsed_label, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(c->elapsed_label, lv_palette_lighten(LV_PALETTE_GREY, 2), 0);
}

/* ── Public API ───────────────────────────────────────────────────── */

void ui_init(void) {
    lv_obj_t *scr = lv_screen_active();
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x1a1a2e), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);

    /* ── Init styles ── */
    lv_style_init(&style_card);
    lv_style_set_bg_color(&style_card, lv_color_hex(0x16213e));
    lv_style_set_bg_opa(&style_card, LV_OPA_COVER);
    lv_style_set_radius(&style_card, 12);
    lv_style_set_border_width(&style_card, 1);
    lv_style_set_border_color(&style_card, lv_color_hex(0x0f3460));

    lv_style_init(&style_title_bar);
    lv_style_set_bg_color(&style_title_bar, lv_color_hex(0x0f3460));
    lv_style_set_bg_opa(&style_title_bar, LV_OPA_COVER);
    lv_style_set_radius(&style_title_bar, 0);

    /* ── Title bar ── */
    lv_obj_t *title_bar = lv_obj_create(scr);
    lv_obj_add_style(title_bar, &style_title_bar, 0);
    lv_obj_set_size(title_bar, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_pad_all(title_bar, 16, 0);
    lv_obj_align(title_bar, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_flex_flow(title_bar, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(title_bar, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(title_bar, LV_OBJ_FLAG_SCROLLABLE);

    title_label = lv_label_create(title_bar);
    lv_label_set_text(title_label, "KPI Dashboard");
    lv_obj_set_style_text_font(title_label, &lv_font_montserrat_36, 0);
    lv_obj_set_style_text_color(title_label, lv_color_white(), 0);

    clock_label = lv_label_create(title_bar);
    lv_label_set_text(clock_label, "00:00:00");
    lv_obj_set_style_text_font(clock_label, &lv_font_montserrat_36, 0);
    lv_obj_set_style_text_color(clock_label, lv_palette_lighten(LV_PALETTE_GREY, 2), 0);

    /* ── Image widget row ── */
    lv_obj_t *img_row = lv_obj_create(scr);
    lv_obj_remove_style_all(img_row);
    lv_obj_set_width(img_row, LV_PCT(100));
    lv_obj_set_height(img_row, LV_SIZE_CONTENT);
    lv_obj_align_to(img_row, title_bar, LV_ALIGN_OUT_BOTTOM_MID, 0, 0);
    lv_obj_set_flex_flow(img_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(img_row, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_all(img_row, 12, 0);
    lv_obj_clear_flag(img_row, LV_OBJ_FLAG_SCROLLABLE);

    create_image_card(img_row, IMG_NVME, "NVMe");
    create_image_card(img_row, IMG_HDD,  "HDD");
    create_image_card(img_row, IMG_SSD,  "SSD");

    /* ── Client list (scrollable area below images) ── */
    client_list = lv_obj_create(scr);
    lv_obj_remove_style_all(client_list);
    lv_obj_set_size(client_list, LV_PCT(100), LV_PCT(100));
    lv_obj_align_to(client_list, img_row, LV_ALIGN_OUT_BOTTOM_MID, 0, 0);
    lv_obj_set_flex_flow(client_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(client_list, 20, 0);
    lv_obj_set_style_pad_row(client_list, 12, 0);
    lv_obj_add_flag(client_list, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(client_list, LV_DIR_VER);
}

void ui_update(registry_t *reg) {
    update_clock();

    registry_lock(reg);

    for (int i = 0; i < reg->count; i++) {
        client_info_t *c = &reg->clients[i];
        if (!c->active) continue;

        /* Create card on first encounter */
        if (c->container == NULL) {
            create_client_card(c);
        }

        /* Update health LED */
        time_t now = time(NULL);
        if (now - c->last_health > HEALTH_TIMEOUT_SEC) {
            lv_led_set_color(c->status_led, lv_palette_main(LV_PALETTE_RED));
        } else {
            lv_led_set_color(c->status_led, lv_palette_main(LV_PALETTE_GREEN));
        }

        /* Update task */
        if (c->task[0] == '\0') {
            /* No active task — show last completed if recent (< 60s) */
            if (c->last_task[0] != '\0' && c->last_task_completed > 0 &&
                (now - c->last_task_completed) < 60) {
                char done_buf[TASK_LEN + 16];
                snprintf(done_buf, sizeof(done_buf), "[done] %s", c->last_task);
                lv_label_set_text(c->task_label, done_buf);
                lv_obj_set_style_text_color(c->task_label,
                    lv_palette_main(LV_PALETTE_GREEN), 0);
                lv_label_set_text(c->elapsed_label, "done");
            } else {
                lv_label_set_text(c->task_label, "(no task)");
                lv_obj_set_style_text_color(c->task_label,
                    lv_palette_lighten(LV_PALETTE_GREY, 2), 0);
                lv_label_set_text(c->elapsed_label, "--:--:--");
            }
        } else {
            lv_label_set_text(c->task_label, c->task);
            lv_obj_set_style_text_color(c->task_label,
                lv_palette_lighten(LV_PALETTE_GREY, 2), 0);
            char elapsed_buf[16];
            format_elapsed(c->task_start, elapsed_buf, sizeof(elapsed_buf));
            lv_label_set_text(c->elapsed_label, elapsed_buf);
        }
    }

    registry_unlock(reg);
}
