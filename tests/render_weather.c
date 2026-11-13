/* render_weather.c — offline render of the weather filler to BMP (sprint 034).
 * Not a test: a review tool, built with the full native tree (it needs LVGL),
 * so the card can be compared with the mock-up without a Pi.
 *
 *   render_weather <out.bmp> [ok|nws|polar|stale] [1|2|3|4]
 *
 * The four states are the mock-up's (wx-C-states.png, top to bottom):
 *   ok     the live record kweather published on 2026-11-12
 *          (tests/fixtures/weather-home-2026-11-12.json), read 4 minutes on
 *   nws    the schema's examples[1]: no current reading, one-sided days
 *   polar  the schema's examples[2]: one day, no sunrise
 *   stale  the live record read two hours on
 * Times are drawn in America/Los_Angeles, rpi53's zone. The canvas is the
 * card at that many columns plus a 16 px margin, on the panel's background. */
#include <cjson/cJSON.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "bmp_write.h"
#include "lvgl.h"
#include "widgets/weather_card.h"
#include "wxstate.h"

static char *slurp(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f)
        return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *buf = malloc((size_t)n + 1);
    if (buf && fread(buf, 1, (size_t)n, f) != (size_t)n) {
        free(buf);
        buf = NULL;
    }
    if (buf)
        buf[n] = '\0';
    fclose(f);
    return buf;
}

static char *schema_example(int i) {
    char *text = slurp(KDASH_WEATHER_SCHEMA);
    cJSON *root = text ? cJSON_Parse(text) : NULL;
    free(text);
    cJSON *ex = cJSON_GetArrayItem(cJSON_GetObjectItemCaseSensitive(root, "examples"), i);
    char *out = ex ? cJSON_PrintUnformatted(ex) : NULL;
    cJSON_Delete(root);
    return out;
}

static void flush_cb(lv_display_t *d, const lv_area_t *a, uint8_t *px) {
    (void)a;
    (void)px;
    lv_display_flush_ready(d);
}

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "usage: %s out.bmp [ok|nws|polar|stale] [1|2|3|4]\n", argv[0]);
        return 2;
    }
    const char *state = argc > 2 ? argv[2] : "ok";
    int ncols = argc > 3 ? atoi(argv[3]) : 3;
    if (ncols < 1 || ncols > WX_MAX_COLS)
        ncols = 3;
    setenv("TZ", "America/Los_Angeles", 1);
    tzset();

    char *json;
    double after; /* seconds after the record's ts */
    if (strcmp(state, "nws") == 0) {
        json = schema_example(1);
        after = 2 * 60;
    } else if (strcmp(state, "polar") == 0) {
        json = schema_example(2);
        after = 9 * 60;
    } else {
        json = slurp(WX_LIVE_FIXTURE);
        after = strcmp(state, "stale") == 0 ? 2 * 3600 + 30 : 4 * 60 + 10;
    }
    wx_reset();
    if (!json || wx_observe(json) != 1) {
        fprintf(stderr, "cannot read the %s record\n", state);
        return 1;
    }
    free(json);
    double now = wx_record()->ts + after;

    lv_init();
    const int32_t W = wx_width(ncols) + 32, H = WX_H + 32;
    lv_display_t *disp = lv_display_create(W, H);
    static uint8_t buf[1520 * 64 * 4];
    lv_display_set_buffers(disp, buf, NULL, sizeof(buf), LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_flush_cb(disp, flush_cb);
    lv_obj_t *scr = lv_screen_active();
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x1E1E2E), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);

    lv_obj_t *card = weather_card_create(scr);
    lv_obj_set_pos(card, 16, 16);
    weather_card_update(card, ncols, now);

    lv_draw_buf_t *snap = lv_snapshot_take(scr, LV_COLOR_FORMAT_XRGB8888);
    if (!snap) {
        fprintf(stderr, "snapshot failed\n");
        return 1;
    }
    FILE *f = fopen(argv[1], "wb");
    bool ok = f && bmp_write_xrgb8888(f, snap->data, (int)snap->header.w, (int)snap->header.h,
                                      (int)snap->header.stride);
    if (f)
        fclose(f);
    lv_draw_buf_destroy(snap);
    return ok ? 0 : 1;
}
