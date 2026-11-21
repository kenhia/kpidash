/* render_service_card.c — offline render of the footer strip's service cards
 * to BMP (WI #4847, sprint 048).
 * Not a test of the model (test_svclayout and test_service_card are): a review
 * tool, built with the full native tree (it needs LVGL), so the card can be
 * judged without a Pi.
 *
 *   render_service_card <out.bmp> [argus|fail|plain|long|full]
 *
 *   argus  an ordinary card, then the argus card at span 1 and at span 2:
 *          a small blue notice, a blank, two left-aligned checks
 *   fail   the same, with the second check a red FAIL line
 *   plain  three ordinary cards, one without a host and one without an icon
 *   long   lines too long for the card, cut with "..." at span 1 and span 2
 *   full   eight L lines under the full header: the bottom ones dropped whole
 *
 * Every state also checks, against LVGL's own layout, that each drawn line
 * sits wholly inside its card's content area and is one line high: it exits
 * non-zero if not. */
#include <stdio.h>
#include <string.h>

#include "bmp_write.h"
#include "lvgl.h"
#include "registry.h"
#include "svclayout.h"
#include "widgets/common.h"
#include "widgets/service_card.h"

#define NOW 1800000000.0
#define CANVAS_W (4 * SVC_CARD_W + 5 * SVC_STRIP_GAP)
#define CANVAS_H (SVC_CARD_H + 2 * SVC_STRIP_GAP)

static void flush_cb(lv_display_t *d, const lv_area_t *a, uint8_t *px) {
    (void)a;
    (void)px;
    lv_display_flush_ready(d);
}

/* One record as a client would publish it, read by kdashdata's parser — the
 * one redis_parse_service_payload() reads the layout with. (That function is
 * test_service_card's; linking redis.c here would drag in the poll layer.) */
static void put(const char *name, const char *host, const char *json) {
    kdash_service_t k;
    if (!kdash_parse_service(json, strlen(json), &k)) {
        fprintf(stderr, "fixture for %s did not parse\n", name);
        return;
    }
    service_entry_t parsed;
    memset(&parsed, 0, sizeof(parsed));
    parsed.last_valid_state = service_parse_state(kdash_service_state_str(k.state));
    parsed.last_payload_ts = NOW - 10;
    memcpy(parsed.text, k.text, sizeof(parsed.text));
    parsed.icon_index = k.has_icon ? k.icon : -1;
    memcpy(parsed.title, k.title, sizeof(parsed.title));
    parsed.span = k.span;
    parsed.n_lines = k.n_lines;
    memcpy(parsed.lines, k.lines, sizeof(parsed.lines));
    service_entry_t *e = service_registry_find_or_create(name, host);
    if (e) service_registry_apply_payload(e, &parsed);
}

static const char *ARGUS_OK =
    "{\"ts\":0,\"state\":\"ok\",\"text\":\"2 checks ok\",\"icon\":%d,\"span\":%d,"
    "\"lines\":["
    "{\"t\":\"blip 12m ago - 3 recent\",\"size\":\"S\",\"color\":\"#1e66f5\"},"
    "\"\","
    "{\"t\":\"OK   backups 02:10\",\"align\":\"L\"},"
    "{\"t\":\"OK   certs 61 d\",\"align\":\"L\"}]}";

static const char *ARGUS_FAIL =
    "{\"ts\":0,\"state\":\"unhealthy\",\"text\":\"1 check failing\",\"icon\":%d,\"span\":%d,"
    "\"lines\":["
    "{\"t\":\"blip 12m ago - 3 recent\",\"size\":\"S\",\"color\":\"#1e66f5\"},"
    "\"\","
    "{\"t\":\"OK   backups 02:10\",\"align\":\"L\"},"
    "{\"t\":\"FAIL certs 2 d\",\"align\":\"L\",\"color\":\"#d20f39\"}]}";

static void argus(const char *fmt) {
    char buf[1024];
    put("korg", "kubs0", "{\"ts\":0,\"state\":\"ok\",\"text\":\"pulse 3s\",\"icon\":2}");
    snprintf(buf, sizeof(buf), fmt, 5, 1);
    put("argus", "kai", buf);
    snprintf(buf, sizeof(buf), fmt, 5, 2);
    put("argus", "kubs0", buf);
}

static int check_lines(void) {
    int bad = 0;
    service_entry_t svcs[SERVICE_REGISTRY_MAX];
    int n = service_registry_snapshot(svcs, SERVICE_REGISTRY_MAX);
    for (int i = 0; i < n; i++) {
        service_entry_t *e = service_registry_find_or_create(svcs[i].name, svcs[i].host);
        lv_obj_t *cont = e->container;
        int32_t ch = lv_obj_get_content_height(cont);
        int32_t cw = lv_obj_get_content_width(cont);
        int shown = 0;
        for (int k = 0; k < KDASH_SVC_LINES_MAX; k++) {
            lv_obj_t *ln = e->line_labels[k];
            if (!ln || lv_obj_has_flag(ln, LV_OBJ_FLAG_HIDDEN)) continue;
            shown++;
            int32_t y = lv_obj_get_y(ln), h = lv_obj_get_height(ln);
            const lv_font_t *f = lv_obj_get_style_text_font(ln, 0);
            if (y < 0 || y + h > ch || lv_obj_get_width(ln) > cw) {
                fprintf(stderr, "%s:%s line %d outside the card (y %d h %d, room %d)\n",
                        e->name, e->host, k, (int)y, (int)h, (int)ch);
                bad++;
            }
            if (h != lv_font_get_line_height(f)) {
                fprintf(stderr, "%s:%s line %d is %d px, not one line\n", e->name,
                        e->host, k, (int)h);
                bad++;
            }
        }
        fprintf(stderr, "%s:%s span %d: %d of %d lines drawn\n", e->name, e->host,
                e->span, shown, e->n_lines);
        if (shown == 0 && e->n_lines > 0) bad++;
    }
    return bad;
}

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "usage: %s <out.bmp> [argus|fail|plain|long|full]\n", argv[0]);
        return 2;
    }
    const char *state = argc > 2 ? argv[2] : "argus";
    if (strcmp(state, "fail") == 0) {
        argus(ARGUS_FAIL);
    } else if (strcmp(state, "plain") == 0) {
        put("korg", "kubs0", "{\"ts\":0,\"state\":\"ok\",\"text\":\"pulse 3s\",\"icon\":2}");
        put("kmon", "_", "{\"ts\":0,\"state\":\"maintenance\",\"text\":\"blip 12m ago, 3 recent\",\"icon\":4}");
        put("redis", "rpi53", "{\"ts\":0,\"state\":\"unhealthy\",\"text\":\"lag 5s\"}");
    } else if (strcmp(state, "long") == 0) {
        static const char *LONG =
            "{\"ts\":0,\"state\":\"ok\",\"text\":\"t\",\"icon\":5,\"span\":%d,"
            "\"title\":\"argus - a title far too long for one unit\",\"lines\":["
            "{\"t\":\"a notice line that runs far past the edge of any card\",\"size\":\"S\"},"
            "{\"t\":\"OK   a check whose description is much too long\",\"align\":\"L\"},"
            "{\"t\":\"right-aligned and also much too long\",\"align\":\"R\",\"size\":\"L\"}]}";
        char buf[1024];
        snprintf(buf, sizeof(buf), LONG, 1);
        put("argus", "kai", buf);
        snprintf(buf, sizeof(buf), LONG, 2);
        put("argus", "kubs0", buf);
    } else if (strcmp(state, "full") == 0) {
        put("eight", "kai",
            "{\"ts\":0,\"state\":\"ok\",\"text\":\"t\",\"icon\":5,\"lines\":["
            "{\"t\":\"one\",\"size\":\"L\"},{\"t\":\"two\",\"size\":\"L\"},"
            "{\"t\":\"three\",\"size\":\"L\"},{\"t\":\"four\",\"size\":\"L\"},"
            "{\"t\":\"five\",\"size\":\"L\"},{\"t\":\"six\",\"size\":\"L\"},"
            "{\"t\":\"seven\",\"size\":\"L\"},{\"t\":\"eight\",\"size\":\"L\"}]}");
        put("eight", "zz",
            "{\"ts\":0,\"state\":\"ok\",\"text\":\"t\",\"title\":\"no icon\",\"lines\":["
            "{\"t\":\"one\",\"size\":\"S\"},{\"t\":\"two\",\"size\":\"S\"},"
            "{\"t\":\"three\",\"size\":\"S\"},{\"t\":\"four\",\"size\":\"S\"},"
            "{\"t\":\"five\",\"size\":\"S\"},{\"t\":\"six\",\"size\":\"S\"},"
            "{\"t\":\"seven\",\"size\":\"S\"},{\"t\":\"eight\",\"size\":\"S\"}]}");
    } else {
        argus(ARGUS_OK);
    }

    lv_init();
    lv_display_t *disp = lv_display_create(CANVAS_W, CANVAS_H);
    static uint8_t buf[CANVAS_W * 64 * 4];
    lv_display_set_buffers(disp, buf, NULL, sizeof(buf), LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_flush_cb(disp, flush_cb);
    lv_obj_t *scr = lv_screen_active();
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x11111B), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);

    /* The strip as ui.c builds it: a flex row with the strip's gap. */
    lv_obj_t *strip = lv_obj_create(scr);
    lv_obj_set_style_bg_opa(strip, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(strip, 0, 0);
    lv_obj_set_style_pad_all(strip, SVC_STRIP_GAP, 0);
    lv_obj_set_style_pad_column(strip, SVC_STRIP_GAP, 0);
    lv_obj_clear_flag(strip, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(strip, CANVAS_W, CANVAS_H);
    lv_obj_set_flex_flow(strip, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(strip, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);

    /* Paint order is the registry's; the fixtures are put in display order. */
    service_entry_t svcs[SERVICE_REGISTRY_MAX];
    int n = service_registry_snapshot(svcs, SERVICE_REGISTRY_MAX);
    for (int i = 0; i < n; i++) {
        service_entry_t *e = service_registry_find_or_create(svcs[i].name, svcs[i].host);
        service_card_create(strip, e);
        service_card_update(e, NOW);
    }
    lv_obj_update_layout(scr);

    lv_draw_buf_t *snap = lv_snapshot_take(scr, LV_COLOR_FORMAT_XRGB8888);
    if (!snap) {
        fprintf(stderr, "snapshot failed\n");
        return 1;
    }
    FILE *f = fopen(argv[1], "wb");
    bool wrote = f && bmp_write_xrgb8888(f, snap->data, (int)snap->header.w,
                                         (int)snap->header.h, (int)snap->header.stride);
    if (f) fclose(f);
    lv_draw_buf_destroy(snap);
    return wrote && check_lines() == 0 ? 0 : 1;
}
