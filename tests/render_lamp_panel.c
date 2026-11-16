/* render_lamp_panel.c — offline render of the lamp panel to BMP (sprint
 * 044, the PDP-8 added in 045). A review tool, built with the full native
 * tree (it needs LVGL), so the panel can be compared with the mock-ups'
 * sprints/planning/altair-widths.png and pdp8-widths.png without a Pi.
 *
 *   render_lamp_panel <out.bmp> <status cards> [seconds] [altair|pdp8]
 *
 * The canvas is the footer: 3840 × 276, the status cards as plain bordered
 * boxes at the strip's real geometry (220 px on a 228 px pitch from x 24,
 * centred in the strip), then the panel in what they leave, run for
 * `seconds` of emulated time (default 10) in LM_REDRAW_MS steps from reset
 * (the Altair from the widget's fixed seed), so a render is reproducible.
 * The machine defaults to the Altair, which is the rotation's first.
 *
 * Also a test: it fails if anything is drawn in the 8 px gap before the
 * panel or right of the strip's edge (x 3816). */
#include <stdio.h>
#include <stdlib.h>

#include <string.h>

#include "bmp_write.h"
#include "lampmach.h"
#include "layout.h"
#include "lvgl.h"
#include "widgets/lamp_panel.h"

#define STRIP_W (SCR_W - 2 * PAD_LEFT)
#define CARD_W 220
#define CARD_GAP 8
#define CARD_Y ((FOOTER_H - LAMP_PANEL_H) / 2)

static void flush_cb(lv_display_t *d, const lv_area_t *a, uint8_t *px) {
    (void)a;
    (void)px;
    lv_display_flush_ready(d);
}

int main(int argc, char **argv) {
    if (argc < 3) {
        fprintf(stderr, "usage: %s <out.bmp> <status cards> [seconds] [altair|pdp8]\n",
                argv[0]);
        return 2;
    }
    int n = atoi(argv[2]);
    int secs = argc > 3 ? atoi(argv[3]) : 10;
    int machine = argc > 4 && strcmp(argv[4], "pdp8") == 0 ? LM_PDP8 : LM_ALTAIR;

    lv_init();
    lv_display_t *disp = lv_display_create(SCR_W, FOOTER_H);
    static uint8_t buf[SCR_W * 32 * 4];
    lv_display_set_buffers(disp, buf, NULL, sizeof(buf), LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_flush_cb(disp, flush_cb);
    lv_obj_t *scr = lv_screen_active();
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x11111B), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);

    for (int i = 0; i < n; i++) {
        lv_obj_t *c = lv_obj_create(scr);
        lv_obj_set_pos(c, PAD_LEFT + i * (CARD_W + CARD_GAP), CARD_Y);
        lv_obj_set_size(c, CARD_W, LAMP_PANEL_H);
        lv_obj_set_style_bg_color(c, lv_color_hex(0x181825), 0);
        lv_obj_set_style_radius(c, 8, 0);
        lv_obj_set_style_border_width(c, 6, 0);
        lv_obj_set_style_border_color(c, lv_color_hex(i % 4 == 3 ? 0xDF8E1D : 0x40A02B), 0);
    }

    lv_obj_t *p = lamp_panel_create(scr);
    lv_obj_set_y(p, CARD_Y);
    int32_t x0 = PAD_LEFT + n * (CARD_W + CARD_GAP), x1 = PAD_LEFT + STRIP_W;
    lamp_panel_place(p, x0, x1 - x0);
    lamp_panel_show(p, machine);
    for (int i = 0; i < secs * 1000 / LM_REDRAW_MS; i++)
        lamp_panel_advance(p, LM_REDRAW_MS);

    static lm_layout_t L;
    LM_MACHINES[machine]->layout(x1 - x0, &L);
    if (L.drawable)
        printf("%s, %d cards: free width %d px, %d lamps, %d labels, first lamp %d x %d px\n",
               LM_MACHINES[machine]->name, n, x1 - x0, L.nlamps, L.nlabels, L.lamps[0].w,
               L.lamps[0].h);
    else
        printf("%s, %d cards: free width %d px, nothing drawn\n", LM_MACHINES[machine]->name, n,
               x1 - x0);

    lv_draw_buf_t *snap = lv_snapshot_take(scr, LV_COLOR_FORMAT_XRGB8888);
    if (!snap) {
        fprintf(stderr, "snapshot failed\n");
        return 1;
    }

    /* The gap before the panel and the margin after the strip stay the
     * screen's background. */
    int stray = 0;
    for (int y = 0; y < FOOTER_H; y++) {
        for (int x = 0; x < SCR_W; x++) {
            bool gap = n > 0 && x >= x0 - CARD_GAP && x < x0;
            if (!gap && x < x1)
                continue;
            const uint8_t *px = snap->data + (size_t)y * snap->header.stride + (size_t)x * 4;
            if (px[0] != 0x1B || px[1] != 0x11 || px[2] != 0x11) /* B, G, R of Crust */
                stray++;
        }
    }
    if (stray)
        fprintf(stderr, "%d pixels drawn outside the panel's span\n", stray);

    FILE *f = fopen(argv[1], "wb");
    bool wrote = f && bmp_write_xrgb8888(f, snap->data, (int)snap->header.w,
                                         (int)snap->header.h, (int)snap->header.stride);
    if (f)
        fclose(f);
    lv_draw_buf_destroy(snap);
    return wrote && !stray ? 0 : 1;
}
