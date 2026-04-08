#include "activities.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

#include "common.h"
#include "protocol.h"

/* Column widths for table layout (624px - 20px pad = 604px internal) */
#define COL_HOST 70
#define COL_DURATION 90
#define COL_DOW 56
#define COL_GAP 8

static const char *DOW_NAMES[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};

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
    if (h > 0)
        snprintf(buf, sizeof(buf), "%dh %02dm", h, m);
    else
        snprintf(buf, sizeof(buf), "%dm %02ds", m, s);
    lv_label_set_text(ud->label, buf);
}

/* Called when the time label is deleted; cancels the timer and frees user data. */
static void elapsed_label_delete_cb(lv_event_t *e) {
    lv_timer_t *tmr = (lv_timer_t *)lv_event_get_user_data(e);
    elapsed_user_data_t *ud = (elapsed_user_data_t *)lv_timer_get_user_data(tmr);
    if (ud) {
        ud->label = NULL; /* guard against any in-flight tick */
        lv_free(ud);
        lv_timer_set_user_data(tmr, NULL);
    }
    lv_timer_delete(tmr);
}

static void format_duration(char *buf, size_t sz, long dur_s) {
    int h = (int)(dur_s / 3600);
    int m = (int)(dur_s % 3600 / 60);
    int s = (int)(dur_s % 60);
    if (h > 0)
        snprintf(buf, sz, "%dh %02dm", h, m);
    else
        snprintf(buf, sz, "%dm %02ds", m, s);
}

static const char *dow_from_ts(double ts) {
    time_t t = (time_t)ts;
    struct tm *tm = localtime(&t);
    if (!tm)
        return "???";
    return DOW_NAMES[tm->tm_wday];
}

lv_obj_t *activities_widget_create(lv_obj_t *parent) {
    lv_obj_t *cont = lv_obj_create(parent);
    lv_obj_clear_flag(cont, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(cont, WS_COLOR_BG, 0);
    lv_obj_set_style_radius(cont, 8, 0);
    lv_obj_set_style_border_color(cont, WS_COLOR_SURFACE1, 0);
    lv_obj_set_style_border_width(cont, 1, 0);
    lv_obj_set_style_pad_all(cont, 10, 0);
    lv_obj_set_style_pad_row(cont, 4, 0);
    lv_obj_set_flex_flow(cont, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(cont, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

    /* Header */
    lv_obj_t *hdr = lv_label_create(cont);
    lv_label_set_text(hdr, "Activities");
    lv_obj_set_style_text_color(hdr, WS_COLOR_HEADER, 0);
    lv_obj_set_style_text_font(hdr, WS_FONT_HDR, 0);
    lv_obj_clear_flag(hdr, LV_OBJ_FLAG_SCROLLABLE);

    /* Placeholder */
    lv_obj_t *empty = lv_label_create(cont);
    lv_label_set_text(empty, "No activities");
    lv_obj_set_style_text_color(empty, WS_COLOR_MUTED, 0);
    lv_obj_set_style_text_font(empty, WS_FONT_BODY, 0);
    lv_obj_clear_flag(empty, LV_OBJ_FLAG_SCROLLABLE);

    return cont;
}

/* ---- Cache: skip rebuild when data unchanged ---- */
static activity_t s_cache[ACTIVITY_MAX_DISPLAY];
static int s_cache_count = -1; /* -1 = never drawn */

/* Compare activity entries ignoring the volatile start_ts/end_ts fields.
 * The elapsed timer handles live time display independently. */
static bool activities_equal(const activity_t *a, const activity_t *b, int n) {
    for (int i = 0; i < n; i++) {
        if (strncmp(a[i].activity_id, b[i].activity_id, ACTIVITY_ID_LEN) != 0)
            return false;
        if (strncmp(a[i].name, b[i].name, ACTIVITY_NAME_LEN) != 0)
            return false;
        if (strncmp(a[i].host, b[i].host, HOSTNAME_LEN) != 0)
            return false;
        if (a[i].is_done != b[i].is_done)
            return false;
    }
    return true;
}

void activities_widget_update(lv_obj_t *widget, const activity_t *list, int count) {
    int show = count < ACTIVITY_MAX_DISPLAY ? count : ACTIVITY_MAX_DISPLAY;

    /* Skip rebuild if data identical to last draw */
    if (show == s_cache_count && (show == 0 || activities_equal(s_cache, list, show)))
        return;

    /* Cache the new data */
    s_cache_count = show;
    if (show > 0)
        memcpy(s_cache, list, (size_t)show * sizeof(activity_t));

    /* Remove all children after header (index 0) */
    uint32_t n = lv_obj_get_child_count(widget);
    for (uint32_t i = n; i > 1; i--) {
        lv_obj_delete(lv_obj_get_child(widget, i - 1));
    }

    if (count == 0) {
        lv_obj_t *empty = lv_label_create(widget);
        lv_label_set_text(empty, "No activities");
        lv_obj_set_style_text_color(empty, WS_COLOR_MUTED, 0);
        lv_obj_set_style_text_font(empty, WS_FONT_BODY, 0);
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
        lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START);
        lv_obj_set_style_pad_column(row, COL_GAP, 0);

        /* Column 1: Host (muted) */
        lv_obj_t *host_lbl = lv_label_create(row);
        lv_label_set_text(host_lbl, a->host);
        lv_obj_set_style_text_color(host_lbl, WS_COLOR_MUTED, 0);
        lv_obj_set_style_text_font(host_lbl, WS_FONT_BODY, 0);
        lv_obj_set_width(host_lbl, COL_HOST);
        lv_label_set_long_mode(host_lbl, LV_LABEL_LONG_CLIP);
        lv_obj_clear_flag(host_lbl, LV_OBJ_FLAG_SCROLLABLE);

        /* Column 2: Duration (white) */
        lv_obj_t *dur_lbl = lv_label_create(row);
        lv_obj_set_style_text_color(dur_lbl, WS_COLOR_FG, 0);
        lv_obj_set_style_text_font(dur_lbl, WS_FONT_BODY, 0);
        lv_obj_set_width(dur_lbl, COL_DURATION);
        lv_obj_clear_flag(dur_lbl, LV_OBJ_FLAG_SCROLLABLE);

        if (a->is_done && a->end_ts > 0 && a->start_ts > 0) {
            char tbuf[32];
            format_duration(tbuf, sizeof(tbuf), (long)(a->end_ts - a->start_ts));
            lv_label_set_text(dur_lbl, tbuf);
        } else if (!a->is_done) {
            char ibuf[32];
            format_duration(ibuf, sizeof(ibuf), (long)((double)time(NULL) - a->start_ts));
            lv_label_set_text(dur_lbl, ibuf);
            elapsed_user_data_t *ud = lv_malloc(sizeof(*ud));
            ud->start_ts = a->start_ts;
            ud->label = dur_lbl;
            lv_timer_t *tmr = lv_timer_create(elapsed_timer_cb, 1000, ud);
            lv_obj_add_event_cb(dur_lbl, elapsed_label_delete_cb, LV_EVENT_DELETE, tmr);
        } else {
            lv_label_set_text(dur_lbl, "--");
        }

        /* Column 3: Day of Week (white) */
        lv_obj_t *dow_lbl = lv_label_create(row);
        lv_label_set_text(dow_lbl, dow_from_ts(a->start_ts));
        lv_obj_set_style_text_color(dow_lbl, WS_COLOR_FG, 0);
        lv_obj_set_style_text_font(dow_lbl, WS_FONT_BODY, 0);
        lv_obj_set_width(dow_lbl, COL_DOW);
        lv_obj_clear_flag(dow_lbl, LV_OBJ_FLAG_SCROLLABLE);

        /* Column 4: Activity name (color = state) */
        lv_obj_t *name_lbl = lv_label_create(row);
        lv_label_set_text(name_lbl, a->name);
        lv_obj_set_style_text_color(name_lbl, a->is_done ? WS_COLOR_BLUE : WS_COLOR_GREEN, 0);
        lv_obj_set_style_text_font(name_lbl, WS_FONT_BODY, 0);
        lv_obj_set_flex_grow(name_lbl, 1);
        lv_label_set_long_mode(name_lbl, LV_LABEL_LONG_CLIP);
        lv_obj_clear_flag(name_lbl, LV_OBJ_FLAG_SCROLLABLE);
    }
}
