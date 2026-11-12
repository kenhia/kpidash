/**
 * host_columns.c — The host columns widget (WI #4524, #4525). See the header.
 *
 * Coordinates below are the mock-up's (sprints/planning/hosts-columns-
 * mockups.html), which measures from the 1896 x 628 cell block. The card this
 * widget's root draws is inset CELL_PAD from that block, so child positions
 * subtract it once, in SLOT_X/SLOT_Y; everything inside a slot is relative to
 * the slot and copied from `colFull` / `tile` unchanged. Text y values are the
 * SVG's baselines, which text_at() converts to LVGL's top-of-line.
 */
#include "host_columns.h"

#include <stdlib.h>
#include <string.h>

#include "common.h"
#include "hosthist.h"
#include "layout.h"

/* ---- Geometry (mock-up `widget`) ---- */
#define HC_W UNIT_W_N(3) /* 1896 */
#define HC_H UNIT_H      /* 628 */
#define HC_CW 175        /* column width */
#define HC_GAP 10
#define HC_X0 28
#define HC_Y0 16
#define HC_CH 596                                   /* column height */
#define HC_TH ((HC_CH - 2 * HC_GAP) / HOSTCOLS_PER) /* tile height, 192 */
#define SLOT_X(col) (HC_X0 - CELL_PAD + (col) * (HC_CW + HC_GAP))
#define SLOT_Y(row) (HC_Y0 - CELL_PAD + (row) * (HC_TH + HC_GAP))

/* Column fill: Catppuccin's raised surface (#232336) without a new colour —
 * Surface0 at 26 % over the card's Base lands on #232335. */
#define HC_RAISE_OPA 66
#define HC_DIM_OPA LV_OPA_40 /* metrics of a host that stopped reporting */

#define FONT_LABEL (&lv_font_montserrat_14)
#define FONT_LABEL_B (&lv_font_montserrat_bold_14)

typedef struct {
    hc_host_t m;
    bool compact;
    int nhist;
    float cpu[HOSTHIST_LEN];
    float core[HOSTHIST_LEN];
    float gpu[HOSTHIST_LEN];
} slot_t;

typedef struct {
    lv_obj_t *obj[HOSTCOLS_MAX];
    slot_t slot[HOSTCOLS_MAX];
    slot_t scratch;
} hc_widget_t;

/* ---- Drawing helpers ---- */

static lv_color_t att_color(hc_att_t a) {
    return a == HC_ATT_CRIT ? WS_COLOR_RED : WS_COLOR_YELLOW;
}

static lv_opa_t mul_opa(lv_opa_t a, lv_opa_t b) {
    return (lv_opa_t)((a * b) / 255);
}

static void rect(lv_layer_t *layer, int32_t x, int32_t y, int32_t w, int32_t h, lv_color_t c,
                 lv_opa_t opa, int32_t radius) {
    if (w <= 0 || h <= 0)
        return;
    lv_draw_rect_dsc_t d;
    lv_draw_rect_dsc_init(&d);
    d.bg_color = c;
    d.bg_opa = opa;
    d.radius = radius;
    lv_area_t a = {x, y, x + w - 1, y + h - 1};
    lv_draw_rect(layer, &d, &a);
}

/* Text whose BASELINE is at y, between x1 and x2, aligned left or right.
 * Truncated with ".." rather than wrapped: a second line would draw over the
 * row below it. */
static void text_at(lv_layer_t *layer, const lv_font_t *font, int32_t x1, int32_t x2,
                    int32_t baseline, const char *s, lv_color_t c, lv_opa_t opa,
                    lv_text_align_t align, int32_t letter_space) {
    if (!s || !s[0])
        return;
    char buf[OS_NAME_LEN];
    size_t len = strnlen(s, sizeof(buf) - 3);
    memcpy(buf, s, len);
    buf[len] = '\0';
    int32_t maxw = x2 - x1;
    if (lv_text_get_width(buf, (uint32_t)len, font, letter_space) > maxw) {
        while (len > 0) {
            len--;
            memcpy(buf + len, "..", 3);
            if (lv_text_get_width(buf, (uint32_t)len + 2, font, letter_space) <= maxw)
                break;
        }
    }

    lv_draw_label_dsc_t d;
    lv_draw_label_dsc_init(&d);
    d.text = buf;
    d.text_local = 1;
    d.font = font;
    d.color = c;
    d.opa = opa;
    d.align = align;
    d.letter_space = letter_space;
    int32_t top = baseline - (font->line_height - font->base_line);
    lv_area_t a = {x1, top, x2, top + font->line_height - 1};
    lv_draw_label(layer, &d, &a);
}

/* The mock-up's `bar`: a rounded track with a fill at least as wide as tall. */
static void bar(lv_layer_t *layer, int32_t x, int32_t y, int32_t w, int32_t h, int pct,
                lv_color_t c, lv_opa_t opa) {
    rect(layer, x, y, w, h, WS_COLOR_SURFACE0, opa, h / 2);
    if (pct <= 0)
        return;
    int32_t fw = w * (pct > 100 ? 100 : pct) / 100;
    if (fw < h)
        fw = h;
    rect(layer, x, y, fw, h, c, opa, h / 2);
}

/* A sparkline over the full HOSTHIST_LEN window, newest sample at the right
 * edge; a history still filling grows in from the right. Optional area fill
 * under the line at fill_opa. */
static void spark(lv_layer_t *layer, int32_t x, int32_t y, int32_t w, int32_t h, const float *v,
                  int n, lv_color_t c, int32_t lw, lv_opa_t fill_opa, lv_opa_t opa) {
    if (n <= 0)
        return;
    const int span = HOSTHIST_LEN - 1;
    const int first = HOSTHIST_LEN - n; /* window index of v[0] */
#define SX(i) (x + (int32_t)(((first + (i)) * w) / span))
#define SY(val) (y + h - (int32_t)(((val) < 0 ? 0 : (val) > 100 ? 100 : (val)) * h / 100.0f))

    if (fill_opa > 0) {
        lv_opa_t fo = mul_opa(fill_opa, opa);
        for (int32_t px = SX(0); px <= x + w; px++) {
            float t = (float)(px - x) * span / w - first;
            if (t < 0)
                continue;
            int i = (int)t;
            float f = t - i;
            float val = (i + 1 < n) ? v[i] * (1 - f) + v[i + 1] * f : v[n - 1];
            int32_t top = SY(val);
            rect(layer, px, top, 1, y + h - top, c, fo, 0);
        }
    }

    lv_draw_line_dsc_t d;
    lv_draw_line_dsc_init(&d);
    d.color = c;
    d.width = lw;
    d.opa = opa;
    d.round_start = 1;
    d.round_end = 1;
    for (int i = 1; i < n; i++) {
        d.p1.x = SX(i - 1);
        d.p1.y = SY(v[i - 1]);
        d.p2.x = SX(i);
        d.p2.y = SY(v[i]);
        lv_draw_line(layer, &d);
    }
#undef SX
#undef SY
}

static void frame(lv_layer_t *layer, const lv_area_t *a, hc_att_t att) {
    lv_draw_rect_dsc_t d;
    lv_draw_rect_dsc_init(&d);
    d.radius = 8;
    d.bg_color = WS_COLOR_SURFACE0;
    d.bg_opa = HC_RAISE_OPA;
    d.border_color = att ? att_color(att) : WS_COLOR_SURFACE1;
    d.border_width = att ? 3 : 1;
    d.border_opa = LV_OPA_COVER;
    lv_draw_rect(layer, &d, a);
}

/* ---- A full column (mock-up `colFull`) ---- */

static void draw_full(lv_layer_t *layer, const lv_area_t *a, const slot_t *s) {
    const hc_host_t *h = &s->m;
    const int32_t x = a->x1, y0 = a->y1;
    const int32_t L = x + 14, R = x + HC_CW - 14, w = HC_CW - 28;
    char buf[32];

    frame(layer, a, h->att);
    rect(layer, x, y0, HC_CW, h->att ? 10 : 6, h->att ? att_color(h->att) : WS_COLOR_GREEN,
         LV_OPA_COVER, 3);

    text_at(layer, WS_FONT_HDR, L, R, y0 + 40, h->host, WS_COLOR_FG, LV_OPA_COVER,
            LV_TEXT_ALIGN_LEFT, 0);
    text_at(layer, FONT_LABEL, L, R, y0 + 60, h->os, WS_COLOR_MUTED, LV_OPA_COVER,
            LV_TEXT_ALIGN_LEFT, 0);
    text_at(layer, h->nodata ? FONT_LABEL_B : FONT_LABEL, L, R, y0 + 80, h->status,
            h->nodata ? WS_COLOR_RED : WS_COLOR_FG, LV_OPA_COVER, LV_TEXT_ALIGN_LEFT, 0);

    const lv_opa_t o = h->nodata ? HC_DIM_OPA : LV_OPA_COVER;

    /* CPU: the average filled, the busiest core a thin fg line over it. */
    text_at(layer, FONT_LABEL, L, R, y0 + 122, "CPU", WS_COLOR_MUTED, o, LV_TEXT_ALIGN_LEFT, 1);
    lv_snprintf(buf, sizeof(buf), "%d%%", h->cpu);
    text_at(layer, WS_FONT_LARGE, L, R, y0 + 124, buf, WS_COLOR_BLUE, o, LV_TEXT_ALIGN_RIGHT, 0);
    spark(layer, L, y0 + 136, w, 60, s->cpu, s->nhist, WS_COLOR_BLUE, 2, 56, o);
    spark(layer, L, y0 + 136, w, 60, s->core, s->nhist, WS_COLOR_FG, 1, 0, o);
    text_at(layer, FONT_LABEL, L, R, y0 + 216, "top core", WS_COLOR_MUTED, o, LV_TEXT_ALIGN_LEFT,
            0);
    lv_snprintf(buf, sizeof(buf), "%d%%", h->core);
    text_at(layer, FONT_LABEL_B, L, R, y0 + 216, buf, WS_COLOR_FG, o, LV_TEXT_ALIGN_RIGHT, 0);

    /* RAM: mauve, or the attention colour with the label bold. */
    {
        bool on = h->ram_att != HC_ATT_NONE;
        lv_color_t rc = att_color(h->ram_att);
        text_at(layer, on ? FONT_LABEL_B : FONT_LABEL, L, R, y0 + 256, "RAM",
                on ? rc : WS_COLOR_MUTED, o, LV_TEXT_ALIGN_LEFT, 1);
        lv_snprintf(buf, sizeof(buf), "%d%%", h->ram_pct);
        text_at(layer, &lv_font_montserrat_bold_16, L, R, y0 + 256, buf, on ? rc : WS_COLOR_FG, o,
                LV_TEXT_ALIGN_RIGHT, 0);
        bar(layer, L, y0 + 266, w, 8, h->ram_pct, on ? rc : WS_COLOR_MAUVE, o);
    }

    if (h->gpu) {
        text_at(layer, FONT_LABEL, L, R, y0 + 308, "GPU", WS_COLOR_MUTED, o, LV_TEXT_ALIGN_LEFT,
                1);
        lv_snprintf(buf, sizeof(buf), "%d%%", h->gpu_pct);
        text_at(layer, WS_FONT_LARGE, L, R, y0 + 310, buf, WS_COLOR_GREEN, o, LV_TEXT_ALIGN_RIGHT,
                0);
        spark(layer, L, y0 + 320, w, 40, s->gpu, s->nhist, WS_COLOR_GREEN, 3, 38, o);
        text_at(layer, FONT_LABEL, L, R, y0 + 386, "VRAM", WS_COLOR_MUTED, o, LV_TEXT_ALIGN_LEFT,
                1);
        text_at(layer, FONT_LABEL, L + 50, R, y0 + 386, h->vram, WS_COLOR_FG, o,
                LV_TEXT_ALIGN_RIGHT, 0);
        bar(layer, L, y0 + 396, w, 8, h->vram_pct, WS_COLOR_ORANGE, o);
    }

    /* Drives sit at the same y in every column, GPU or not. */
    for (int k = 0; k < h->ndisks; k++) {
        const hc_disk_t *d = &h->disks[k];
        int32_t y = y0 + 438 + k * 32;
        bool on = d->att != HC_ATT_NONE;
        lv_color_t dc = att_color(d->att);
        const lv_font_t *f = on ? FONT_LABEL_B : FONT_LABEL;
        lv_snprintf(buf, sizeof(buf), "%d%%", d->pct);
        text_at(layer, f, L, R - 44, y, d->name, on ? dc : WS_COLOR_FG, o, LV_TEXT_ALIGN_LEFT, 0);
        text_at(layer, f, L, R, y, buf, on ? dc : WS_COLOR_MUTED, o, LV_TEXT_ALIGN_RIGHT, 0);
        bar(layer, L, y + 7, w, 6, d->pct, on ? dc : WS_COLOR_LAVENDER, o);
    }
}

/* ---- A compact tile (mock-up `tile`) ---- */

typedef struct {
    const char *label;
    int pct;
    lv_color_t color;
    hc_att_t att;
    int tick; /* top-core marker on the cpu bar; -1 for none */
} tile_row_t;

static void draw_tile(lv_layer_t *layer, const lv_area_t *a, const slot_t *s) {
    const hc_host_t *h = &s->m;
    const int32_t x = a->x1, y = a->y1;
    const int32_t L = x + 12, R = x + HC_CW - 12;
    const int32_t bx = x + 58, bw = HC_CW - 58 - 52;
    char buf[16];

    frame(layer, a, h->att);
    rect(layer, x + 13, y + 23, 12, 12, h->nodata ? WS_COLOR_RED : WS_COLOR_GREEN, LV_OPA_COVER,
         LV_RADIUS_CIRCLE);
    text_at(layer, WS_FONT_HDR, x + 33, R, y + 36, h->host, WS_COLOR_FG, LV_OPA_COVER,
            LV_TEXT_ALIGN_LEFT, 0);
    text_at(layer, h->nodata ? FONT_LABEL_B : FONT_LABEL, L, R, y + 60, h->status,
            h->nodata ? WS_COLOR_RED : WS_COLOR_FG, LV_OPA_COVER, LV_TEXT_ALIGN_LEFT, 0);

    const lv_opa_t o = h->nodata ? HC_DIM_OPA : LV_OPA_COVER;
    tile_row_t rows[4];
    int nr = 0;
    rows[nr++] = (tile_row_t){"cpu", h->cpu, WS_COLOR_BLUE, HC_ATT_NONE, h->core};
    rows[nr++] = (tile_row_t){"ram", h->ram_pct, WS_COLOR_MAUVE, h->ram_att, -1};
    if (h->gpu)
        rows[nr++] = (tile_row_t){"gpu", h->gpu_pct, WS_COLOR_GREEN, HC_ATT_NONE, -1};
    char dname[6] = "";
    if (h->worst >= 0) {
        const hc_disk_t *d = &h->disks[h->worst];
        memcpy(dname, d->name, strnlen(d->name, sizeof(dname) - 1));
        rows[nr++] = (tile_row_t){dname, d->pct, WS_COLOR_LAVENDER, d->att, -1};
    }

    for (int k = 0; k < nr; k++) {
        int32_t yy = y + 92 + k * 26;
        bool on = rows[k].att != HC_ATT_NONE;
        lv_color_t c = att_color(rows[k].att);
        const lv_font_t *f = on ? FONT_LABEL_B : FONT_LABEL;
        text_at(layer, f, L, bx - 2, yy + 5, rows[k].label, on ? c : WS_COLOR_MUTED, o,
                LV_TEXT_ALIGN_LEFT, 0);
        bar(layer, bx, yy - 4, bw, 8, rows[k].pct, on ? c : rows[k].color, o);
        if (rows[k].tick >= 0) {
            int t = rows[k].tick > 100 ? 100 : rows[k].tick;
            rect(layer, bx + bw * t / 100 - 1, yy - 7, 3, 14, WS_COLOR_FG, o, 1);
        }
        lv_snprintf(buf, sizeof(buf), "%d%%", rows[k].pct);
        text_at(layer, f, bx + bw, R, yy + 5, buf, on ? c : WS_COLOR_FG, o, LV_TEXT_ALIGN_RIGHT,
                0);
    }
}

static void slot_draw_cb(lv_event_t *e) {
    lv_obj_t *obj = lv_event_get_target(e);
    const slot_t *s = lv_obj_get_user_data(obj);
    if (!s)
        return;
    lv_area_t a;
    lv_obj_get_coords(obj, &a);
    lv_layer_t *layer = lv_event_get_layer(e);
    if (s->compact)
        draw_tile(layer, &a, s);
    else
        draw_full(layer, &a, s);
}

static void widget_delete_cb(lv_event_t *e) {
    lv_obj_t *w = lv_event_get_target(e);
    free(lv_obj_get_user_data(w));
}

/* ---- Public API ---- */

lv_obj_t *host_columns_create(lv_obj_t *parent, int32_t x, int32_t y) {
    lv_obj_t *w = lv_obj_create(parent);
    lv_obj_clear_flag(w, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_pos(w, x + CELL_PAD, y + CELL_PAD);
    lv_obj_set_size(w, HC_W - 2 * CELL_PAD, HC_H - 2 * CELL_PAD);
    lv_obj_set_style_bg_color(w, WS_COLOR_BG, 0);
    lv_obj_set_style_bg_opa(w, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(w, 8, 0);
    lv_obj_set_style_border_width(w, 1, 0);
    lv_obj_set_style_border_color(w, WS_COLOR_SURFACE0, 0);
    lv_obj_set_style_pad_all(w, 0, 0);

    hc_widget_t *hw = calloc(1, sizeof(*hw));
    lv_obj_set_user_data(w, hw);
    lv_obj_add_event_cb(w, widget_delete_cb, LV_EVENT_DELETE, NULL);
    if (!hw)
        return w;

    for (int i = 0; i < HOSTCOLS_MAX; i++) {
        lv_obj_t *o = lv_obj_create(w);
        lv_obj_remove_style_all(o);
        lv_obj_clear_flag(o, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_user_data(o, &hw->slot[i]);
        lv_obj_add_event_cb(o, slot_draw_cb, LV_EVENT_DRAW_MAIN, NULL);
        hw->obj[i] = o;
    }
    return w;
}

void host_columns_update(lv_obj_t *w, const hc_host_t *hosts, int n) {
    hc_widget_t *hw = w ? lv_obj_get_user_data(w) : NULL;
    if (!hw)
        return;
    if (n < 0)
        n = 0;
    if (n > HOSTCOLS_MAX)
        n = HOSTCOLS_MAX;
    int full, compact;
    hostcols_plan(n, &full, &compact);

    for (int i = 0; i < HOSTCOLS_MAX; i++) {
        lv_obj_t *o = hw->obj[i];
        if (i >= n) {
            lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN);
            continue;
        }

        /* Build the slot's next state in scratch, zeroed so memcmp sees only
         * content, then repaint only when it differs. */
        slot_t *s = &hw->scratch;
        memset(s, 0, sizeof(*s));
        memcpy(&s->m, &hosts[i], sizeof(s->m));
        s->compact = i >= full;
        s->nhist = hosthist_read(hosts[i].host, s->cpu, s->core, s->gpu, HOSTHIST_LEN);

        int32_t px, py, ph;
        if (!s->compact) {
            px = SLOT_X(i);
            py = SLOT_Y(0);
            ph = HC_CH;
        } else {
            int j = i - full;
            px = SLOT_X(full + j / HOSTCOLS_PER);
            py = SLOT_Y(j % HOSTCOLS_PER);
            ph = HC_TH;
        }
        lv_obj_set_pos(o, px, py);
        lv_obj_set_size(o, HC_CW, ph);
        lv_obj_clear_flag(o, LV_OBJ_FLAG_HIDDEN);

        if (memcmp(s, &hw->slot[i], sizeof(*s)) != 0) {
            hw->slot[i] = *s;
            lv_obj_invalidate(o);
        }
    }
}
