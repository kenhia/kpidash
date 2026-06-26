#include "fortune.h"

#include <cjson/cJSON.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "protocol.h"
#include "redis.h"
#include "status.h"
#include "widgets/fortune.h"

/* ---- Canned fortune errors (U2) ---- */
static const char *CANNED_ERRORS[] = {
    "Fortune not found. Please shake device vigorously and try again.",
    "Today's fortune is on strike. It demands better working conditions and fewer cows.",
    "Segmentation fault: fortune tried to divide by zen.",
    "No fortune today. The universe is buffering.",
    "Your fortune wandered off. It left a note: 'BRB, consulting the Oracle.'",
    "404: Wisdom not found. Try enlightenment.exe?",
};
#define CANNED_COUNT ((int)(sizeof(CANNED_ERRORS) / sizeof(CANNED_ERRORS[0])))

/* ---- Font-fit heuristic ---- */

/*
 * Character-per-line and max-line thresholds for each step on the font ladder.
 * The widget is UNIT_W_N(2) x UNIT_H_N(1) = 1264 x 628 px, 16 px padding,
 * label width 95% of content = ~1170 px.  Thresholds are initial estimates;
 * calibrate by pushing test strings via kpidash:fortune:pushed.
 */
#define FORTUNE_FIT_MAX_LINE_36  45
#define FORTUNE_FIT_MAX_LINE_28  60
#define FORTUNE_FIT_MAX_LINE_24  75
#define FORTUNE_FIT_MAX_LINE_20  90

#define FORTUNE_FIT_MAX_LINES_36  5
#define FORTUNE_FIT_MAX_LINES_28  7
#define FORTUNE_FIT_MAX_LINES_24  9
#define FORTUNE_FIT_MAX_LINES_20 11

/* Give up and use a canned message after this many rejected fortunes. */
#define FORTUNE_MAX_RETRIES 10

static const struct {
    int pt;
    int max_line_chars;
    int max_lines;
} FONT_LADDER[] = {
    {36, FORTUNE_FIT_MAX_LINE_36, FORTUNE_FIT_MAX_LINES_36},
    {28, FORTUNE_FIT_MAX_LINE_28, FORTUNE_FIT_MAX_LINES_28},
    {24, FORTUNE_FIT_MAX_LINE_24, FORTUNE_FIT_MAX_LINES_24},
    {20, FORTUNE_FIT_MAX_LINE_20, FORTUNE_FIT_MAX_LINES_20},
    {0,  0, 0} /* sentinel */
};

/*
 * Walk the font ladder (36→28→24→20) and return the first font size (in pt)
 * at which the text fits the character-count heuristic, or -1 to reject.
 *
 * The check: no \n-delimited line exceeds max_line_chars for that size, AND
 * the total explicit line count does not exceed max_lines.  Long lines that
 * would wrap in LVGL are the primary source of visual ugliness — this keeps
 * them out before rendering.
 */
static int fortune_pick_font(const char *text) {
    if (!text || text[0] == '\0')
        return -1;

    for (int fi = 0; FONT_LADDER[fi].pt != 0; fi++) {
        const int max_line  = FONT_LADDER[fi].max_line_chars;
        const int max_lines = FONT_LADDER[fi].max_lines;

        int total_lines    = 1;
        int cur_line_len   = 0;
        int max_observed   = 0;

        for (const char *p = text; *p; p++) {
            if (*p == '\n') {
                if (cur_line_len > max_observed)
                    max_observed = cur_line_len;
                cur_line_len = 0;
                total_lines++;
            } else {
                cur_line_len++;
            }
        }
        if (cur_line_len > max_observed)
            max_observed = cur_line_len;

        if (max_observed <= max_line && total_lines <= max_lines)
            return FONT_LADDER[fi].pt;
    }
    return -1; /* reject */
}

static lv_obj_t *g_widget = NULL;
static bool g_fortune_available = false;
static bool g_pushed_active = false;

/* Stats from the last rotation cycle — used by the dev overlay (Step D). */
static int  g_last_reject_count = 0;
static long g_last_elapsed_ms   = 0;

static const char *fortune_binary(void) {
    static const char *candidates[] = {"/usr/bin/fortune", "/usr/games/fortune", NULL};
    for (int i = 0; candidates[i]; i++) {
        if (access(candidates[i], X_OK) == 0)
            return candidates[i];
    }
    return NULL;
}

static void update_widget(const char *text, int font_size_pt) {
    if (!g_widget)
        return;
    fortune_widget_update(g_widget, text, font_size_pt);
}

static void update_dev_overlay(void) {
    if (!g_widget)
        return;
    const dev_cmd_state_t *cmd = redis_get_dev_cmd_state();
    fortune_widget_set_dev_stats(g_widget, cmd->fortune_dev_enabled,
                                 g_last_reject_count, g_last_elapsed_ms);
}

static void run_fortune_or_canned(void) {
    if (g_fortune_available) {
        const char *bin = fortune_binary();
        if (bin) {
            struct timespec t0, t1;
            clock_gettime(CLOCK_MONOTONIC, &t0);
            int rejects = 0;

            for (int attempt = 0; attempt < FORTUNE_MAX_RETRIES; attempt++) {
                FILE *f = popen(bin, "r");
                if (!f)
                    break;
                char buf[1024] = {0};
                fread(buf, 1, sizeof(buf) - 1, f);
                pclose(f);

                /* Strip trailing newlines */
                size_t n = strlen(buf);
                while (n > 0 && (buf[n - 1] == '\n' || buf[n - 1] == '\r'))
                    buf[--n] = '\0';

                int font_pt = fortune_pick_font(buf);
                if (font_pt < 0) {
                    rejects++;
                    continue;
                }

                /* Accepted — record stats, display, persist */
                clock_gettime(CLOCK_MONOTONIC, &t1);
                g_last_reject_count = rejects;
                g_last_elapsed_ms   = (t1.tv_sec  - t0.tv_sec)  * 1000L
                                    + (t1.tv_nsec - t0.tv_nsec) / 1000000L;
                update_widget(buf, font_pt);
                update_dev_overlay();
                cJSON *root = cJSON_CreateObject();
                cJSON_AddStringToObject(root, "text", buf);
                cJSON_AddStringToObject(root, "source", "local");
                char *json = cJSON_PrintUnformatted(root);
                cJSON_Delete(root);
                if (json) {
                    redis_write_fortune_current(json);
                    free(json);
                }
                return;
            }
        }
    }
    /* Binary not found, popen failed, or all retries rejected: use canned */
    g_last_reject_count = FORTUNE_MAX_RETRIES;
    g_last_elapsed_ms   = 0;
    int idx = (int)(time(NULL) % CANNED_COUNT);
    update_widget(CANNED_ERRORS[idx], 36);
    update_dev_overlay();
}

static void fortune_timer_cb(lv_timer_t *t) {
    (void)t;
    if (g_pushed_active) {
        /* Pushed fortune still being displayed; don't override */
        g_pushed_active = false; /* will be re-set by fortune_on_pushed() if still in Redis */
        return;
    }
    run_fortune_or_canned();
}

void fortune_init(const kpidash_config_t *cfg) {
    (void)cfg; /* fortune interval is compile-time FORTUNE_INTERVAL_S */

    const char *bin = fortune_binary();
    if (!bin) {
        status_push(STATUS_WARNING, "fortune binary not found — using canned messages");
        g_fortune_available = false;
    } else {
        g_fortune_available = true;
    }

    lv_timer_create(fortune_timer_cb, FORTUNE_INTERVAL_S * 1000, NULL);
    /* Run immediately for the first display */
    run_fortune_or_canned();
}

void fortune_set_widget(lv_obj_t *widget) {
    g_widget = widget;
}

void fortune_on_pushed(const char *json) {
    cJSON *root = cJSON_Parse(json);
    if (!root)
        return;

    cJSON *text_obj = cJSON_GetObjectItemCaseSensitive(root, "text");
    if (cJSON_IsString(text_obj) && text_obj->valuestring) {
        /* Apply font ladder; pushed fortunes are never rejected — fall back to 20pt */
        int font_pt = fortune_pick_font(text_obj->valuestring);
        if (font_pt < 0) font_pt = 20;
        update_widget(text_obj->valuestring, font_pt);
        update_dev_overlay();
        g_pushed_active = true;
    }
    cJSON_Delete(root);
}
