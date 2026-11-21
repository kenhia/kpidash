/* render_apt_temps.c — offline render of the apt-temps widget to BMP
 * (WI #4848, sprint 047).
 * Not a test of the model (test_atstate is): a review tool, built with the
 * full native tree (it needs LVGL), so the widget can be judged without a Pi.
 *
 *   render_apt_temps <out.bmp> [normal|bands|stale|missing|unknown|extremes]
 *
 *   normal    all three zones fresh and comfortable
 *   bands     one zone in each of cold, warm and hot
 *   stale     the kitchen's reading 6 minutes old: gray
 *   missing   no bedroom key at all: its slot muted, "no reading"
 *   unknown   all three fresh plus a fourth, unnamed sensor: the border green
 *   extremes  -40.0° / 140.0° / 100%: the widest readings the panel can meet
 *
 * The canvas is the widget's whole 376 × 628 cell, the widget at CELL_PAD
 * inside it, on the panel's crust. Every state also checks that the widest
 * temperature, each zone's name and the widest humidity line fit inside the
 * widget: it exits non-zero if not. */
#include <stdio.h>
#include <string.h>

#include "atstate.h"
#include "bmp_write.h"
#include "layout.h"
#include "lvgl.h"
#include "registry.h"
#include "widgets/apt_temps_card.h"
#include "widgets/common.h"

#define NOW 1800000000.0

static void flush_cb(lv_display_t *d, const lv_area_t *a, uint8_t *px) {
    (void)a;
    (void)px;
    lv_display_flush_ready(d);
}

static void put(const char *slug, float t, int h, double age) {
    apttemps_entry_t p;
    memset(&p, 0, sizeof(p));
    p.temp_f = t;
    p.humidity_pct = h;
    p.last_payload_ts = NOW - age;
    p.valid = true;
    snprintf(p.zone, sizeof(p.zone), "%s", slug);
    apttemps_entry_t *e = apttemps_registry_find_or_create(slug);
    if (e)
        apttemps_registry_apply_payload(e, &p);
}

static int32_t width(const lv_font_t *f, const char *s) {
    return lv_text_get_width(s, (uint32_t)strlen(s), f, 0);
}

/* The widest things the widget can be asked to draw, against its room. Since
 * WI #4870 each is on its own line and centred, so each meets the whole
 * width less a margin on either side. */
static int fits(void) {
    int bad = 0;
    const int32_t room = AT_CARD_W - 2 * 24;
    static const char *const TEMPS[] = {"-40.0\xC2\xB0" "F", "140.0\xC2\xB0" "F",
                                        "100.0\xC2\xB0" "F", "88.8\xC2\xB0" "F"};
    for (size_t i = 0; i < sizeof(TEMPS) / sizeof(TEMPS[0]); i++) {
        int32_t w = width(&lv_font_montserrat_bold_48, TEMPS[i]);
        if (w > room) {
            fprintf(stderr, "temperature %s is %d px, room %d\n", TEMPS[i], (int)w, (int)room);
            bad++;
        }
    }
    for (int i = 0; i < AT_ZONES; i++) {
        if (width(&lv_font_montserrat_bold_28, at_zone_name(i)) > room) {
            fprintf(stderr, "%s does not fit\n", at_zone_name(i));
            bad++;
        }
    }
    if (width(&lv_font_montserrat_bold_28, "100%") > room ||
        width(&lv_font_montserrat_24, "no reading") > room) {
        fprintf(stderr, "the humidity line does not fit\n");
        bad++;
    }
    return bad;
}

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "usage: %s <out.bmp> [normal|bands|stale|missing|unknown|extremes]\n",
                argv[0]);
        return 2;
    }
    const char *state = argc > 2 ? argv[2] : "normal";
    if (strcmp(state, "bands") == 0) {
        put("living", 62.4f, 38, 20);
        put("kitchen", 77.1f, 44, 20);
        put("bedroom", 81.6f, 51, 20);
    } else if (strcmp(state, "extremes") == 0) {
        put("living", -40.0f, 0, 20);
        put("kitchen", 140.0f, 100, 20);
        put("bedroom", 100.0f, 100, 20);
    } else {
        put("living", 71.6f, 42, 20);
        put("kitchen", 69.8f, 45, strcmp(state, "stale") == 0 ? 360 : 20);
        if (strcmp(state, "missing") != 0)
            put("bedroom", 67.3f, 48, 20);
        if (strcmp(state, "unknown") == 0)
            put("sensor-a4c138", 70.2f, 40, 5);
    }

    lv_init();
    const int32_t W = ROW2_APT_W, H = UNIT_H;
    lv_display_t *disp = lv_display_create(W, H);
    static uint8_t buf[ROW2_APT_W * 64 * 4];
    lv_display_set_buffers(disp, buf, NULL, sizeof(buf), LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_flush_cb(disp, flush_cb);
    lv_obj_t *scr = lv_screen_active();
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x11111B), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);

    lv_obj_t *card = apt_temps_card_create(scr);
    lv_obj_set_pos(card, CELL_PAD, CELL_PAD);
    apt_temps_card_update(card, NOW);

    lv_draw_buf_t *snap = lv_snapshot_take(scr, LV_COLOR_FORMAT_XRGB8888);
    if (!snap) {
        fprintf(stderr, "snapshot failed\n");
        return 1;
    }
    FILE *f = fopen(argv[1], "wb");
    bool wrote = f && bmp_write_xrgb8888(f, snap->data, (int)snap->header.w,
                                         (int)snap->header.h, (int)snap->header.stride);
    if (f)
        fclose(f);
    lv_draw_buf_destroy(snap);
    return wrote && fits() == 0 ? 0 : 1;
}
