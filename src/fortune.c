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

static lv_obj_t *g_widget = NULL;
static bool g_fortune_available = false;
static bool g_pushed_active = false;

static const char *fortune_binary(void) {
    static const char *candidates[] = {"/usr/bin/fortune", "/usr/games/fortune", NULL};
    for (int i = 0; candidates[i]; i++) {
        if (access(candidates[i], X_OK) == 0)
            return candidates[i];
    }
    return NULL;
}

static void update_widget(const char *text) {
    if (!g_widget)
        return;
    fortune_widget_update(g_widget, text);
}

static void run_fortune_or_canned(void) {
    if (g_fortune_available) {
        const char *bin = fortune_binary();
        if (bin) {
            FILE *f = popen(bin, "r");
            if (f) {
                char buf[1024] = {0};
                fread(buf, 1, sizeof(buf) - 1, f);
                pclose(f);
                /* Strip trailing newlines */
                size_t n = strlen(buf);
                while (n > 0 && (buf[n - 1] == '\n' || buf[n - 1] == '\r')) {
                    buf[--n] = '\0';
                }
                update_widget(buf);
                /* Store in Redis for persistence */
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
    /* Fortune binary not found or failed: use canned */
    int idx = (int)(time(NULL) % CANNED_COUNT);
    update_widget(CANNED_ERRORS[idx]);
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
        update_widget(text_obj->valuestring);
        g_pushed_active = true;
    }
    cJSON_Delete(root);
}
