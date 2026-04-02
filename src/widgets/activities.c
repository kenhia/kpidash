#include "activities.h"
#include "protocol.h"
#include <stdio.h>
#include <string.h>
#include <time.h>

#define COLOR_BG     lv_color_hex(0x1E1E2E)
#define COLOR_FG     lv_color_hex(0xCDD6F4)
#define COLOR_MUTED  lv_color_hex(0x6C7086)
#define COLOR_ACTIVE lv_color_hex(0xA6E3A1)
#define COLOR_DONE   lv_color_hex(0x89B4FA)
#define COLOR_HEADER lv_color_hex(0xF5C2E7)

static const lv_font_t *FONT_HDR  = &lv_font_montserrat_16;
static const lv_font_t *FONT_BODY = &lv_font_montserrat_14;

/* ---- Elapsed timer data attached to each active-row label ---- */
typedef struct {
    double start_ts;
    lv_obj_t *label;
} elapsed_user_data_t;

static void elapsed_timer_cb(lv_timer_t *t) {
    elapsed_user_data_t *ud = (elapsed_user_data_t *)lv_timer_get_user_data(t);
    if (!ud || !ud->label) {
        lv_timer_delete(t);
        return;
    }
    double elapsed = (double)time(NULL) - ud->start_ts;
    int h = (int)(elapsed / 3600);
    int m = (int)((long)elapsed % 3600 / 60);
    int s = (int)((long)elapsed % 60);
    char buf[32];
    snprintf(buf, sizeof(buf), "%02d:%02d:%02d", h, m, s);
    lv_label_set_text(ud->label, buf);
}

/* Called when the time label is deleted; cancels the timer and frees user data. */
static void elapsed_label_delete_cb(lv_event_t *e) {
    lv_timer_t *tmr = (lv_timer_t *)lv_event_get_user_data(e);
    elapsed_user_data_t *ud = (elapsed_user_data_t *)lv_timer_get_user_data(tmr);
    if (ud) {
        ud->label = NULL;   /* guard against any in-flight tick */
        lv_free(ud);
        lv_timer_set_user_data(tmr, NULL);
    }
    lv_timer_delete(tmr);
}

lv_obj_t *activities_widget_create(lv_obj_t *parent) {
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
    lv_label_set_text(hdr, "Activities");
    lv_obj_set_style_text_color(hdr, COLOR_HEADER, 0);
    lv_obj_set_style_text_font(hdr, FONT_HDR, 0);
    lv_obj_clear_flag(hdr, LV_OBJ_FLAG_SCROLLABLE);

    /* Placeholder */
    lv_obj_t *empty = lv_label_create(cont);
    lv_label_set_text(empty, "No activities");
    lv_obj_set_style_text_color(empty, COLOR_MUTED, 0);
    lv_obj_set_style_text_font(empty, FONT_BODY, 0);
    lv_obj_clear_flag(empty, LV_OBJ_FLAG_SCROLLABLE);

    return cont;
}

void activities_widget_update(lv_obj_t *widget, const activity_t *list, int count) {
    /* Remove all children after header (index 0) */
    uint32_t n = lv_obj_get_child_count(widget);
    for (uint32_t i = n; i > 1; i--) {
        lv_obj_delete(lv_obj_get_child(widget, i - 1));
    }

    if (count == 0) {
        lv_obj_t *empty = lv_label_create(widget);
        lv_label_set_text(empty, "No activities");
        lv_obj_set_style_text_color(empty, COLOR_MUTED, 0);
        lv_obj_set_style_text_font(empty, FONT_BODY, 0);
        lv_obj_clear_flag(empty, LV_OBJ_FLAG_SCROLLABLE);
        return;
    }

    for (int i = 0; i < count && i < ACTIVITY_MAX_DISPLAY; i++) {
        const activity_t *a = &list[i];

        /* Row container */
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

        /* State badge */
        lv_obj_t *badge = lv_label_create(row);
        lv_label_set_text(badge, a->is_done ? "[done]" : "[active]");
        lv_obj_set_style_text_color(badge, a->is_done ? COLOR_DONE : COLOR_ACTIVE, 0);
        lv_obj_set_style_text_font(badge, FONT_BODY, 0);
        lv_obj_clear_flag(badge, LV_OBJ_FLAG_SCROLLABLE);

        /* Host + name */
        char info[256];
        snprintf(info, sizeof(info), "%s: %s", a->host, a->name);
        lv_obj_t *name_lbl = lv_label_create(row);
        lv_label_set_text(name_lbl, info);
        lv_obj_set_style_text_color(name_lbl, COLOR_FG, 0);
        lv_obj_set_style_text_font(name_lbl, FONT_BODY, 0);
        lv_obj_set_flex_grow(name_lbl, 1);
        lv_label_set_long_mode(name_lbl, LV_LABEL_LONG_CLIP);
        lv_obj_clear_flag(name_lbl, LV_OBJ_FLAG_SCROLLABLE);

        /* Time label */
        lv_obj_t *time_lbl = lv_label_create(row);
        lv_obj_set_style_text_color(time_lbl, COLOR_MUTED, 0);
        lv_obj_set_style_text_font(time_lbl, FONT_BODY, 0);
        lv_obj_clear_flag(time_lbl, LV_OBJ_FLAG_SCROLLABLE);

        if (a->is_done && a->end_ts > 0 && a->start_ts > 0) {
            long dur = (long)(a->end_ts - a->start_ts);
            char tbuf[32];
            snprintf(tbuf, sizeof(tbuf), "%ldm %02lds",
                     dur / 60, dur % 60);
            lv_label_set_text(time_lbl, tbuf);
        } else if (!a->is_done) {
            lv_label_set_text(time_lbl, "00:00:00");
            /* Register live elapsed timer; tie its lifetime to the label */
            elapsed_user_data_t *ud = lv_malloc(sizeof(*ud));
            ud->start_ts = a->start_ts;
            ud->label    = time_lbl;
            lv_timer_t *tmr = lv_timer_create(elapsed_timer_cb, 1000, ud);
            /* Delete the timer (and free ud) when this label is destroyed */
            lv_obj_add_event_cb(time_lbl, elapsed_label_delete_cb,
                                LV_EVENT_DELETE, tmr);
        } else {
            lv_label_set_text(time_lbl, "--");
        }
    }
}
