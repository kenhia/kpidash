/**
 * host_columns.c — The host columns widget (WI #4524, #4525). See the header.
 *
 * Coordinates below are the mock-up's (sprints/planning/hosts-wide-
 * mockups.html, sprint 030), which measures from the 3792 x 628 cell block. The card this
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
#define HC_W UNIT_W_N(6) /* 3792: the whole top row (WI #4543) */
#define HC_H UNIT_H      /* 628 */
#define HC_CW 361        /* column width */
#define HC_GAP 14
#define HC_X0 28
#define HC_Y0 16
#define HC_CH 596                                   /* column height */
#define HC_TH ((HC_CH - 2 * HC_GAP) / HOSTCOLS_PER) /* tile height, 189 */
#define SLOT_X(col) (HC_X0 - CELL_PAD + (col) * (HC_CW + HC_GAP))
#define SLOT_Y(row) (HC_Y0 - CELL_PAD + (row) * (HC_TH + HC_GAP))

/* Column fill: Catppuccin's raised surface (#232336) without a new colour —
 * Surface0 at 26 % over the card's Base lands on #232335. */
#define HC_RAISE_OPA 66
#define HC_DIM_OPA LV_OPA_40 /* metrics of a host that stopped reporting */

/* Nothing on the widget is smaller than 20 px (WI #4543). */
#define FONT_LABEL (&lv_font_montserrat_20)
#define FONT_LABEL_B (&lv_font_montserrat_bold_20)
#define FONT_NAME (&lv_font_montserrat_bold_28)
#define FONT_MID_B (&lv_font_montserrat_bold_28)
#define FONT_BIG_B (&lv_font_montserrat_bold_36)

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
    if (!s || !s[0] || x2 <= x1)
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

static int32_t text_w(const lv_font_t *font, const char *s, int32_t letter_space) {
    return lv_text_get_width(s, (uint32_t)strlen(s), font, letter_space);
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
    d.radius = 10;
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
    const int32_t L = x + 20, R = x + HC_CW - 20, w = HC_CW - 40;
    char buf[LABEL_LEN + 16];

    frame(layer, a, h->att);
    rect(layer, x, y0, HC_CW, h->att ? 12 : 6, h->att ? att_color(h->att) : WS_COLOR_GREEN,
         LV_OPA_COVER, 3);

    text_at(layer, FONT_NAME, L, R, y0 + 50, h->host, WS_COLOR_FG, LV_OPA_COVER,
            LV_TEXT_ALIGN_LEFT, 0);
    /* Uptime (or NO DATA) left, the OS name muted on the right of the same
     * line; the OS gives way, never the uptime. */
    const lv_font_t *sf = h->nodata ? FONT_LABEL_B : FONT_LABEL;
    text_at(layer, sf, L, R, y0 + 82, h->status, h->nodata ? WS_COLOR_RED : WS_COLOR_FG,
            LV_OPA_COVER, LV_TEXT_ALIGN_LEFT, 0);
    text_at(layer, FONT_LABEL, L + text_w(sf, h->status, 0) + 16, R, y0 + 82, h->os,
            WS_COLOR_MUTED, LV_OPA_COVER, LV_TEXT_ALIGN_RIGHT, 0);

    const lv_opa_t o = h->nodata ? HC_DIM_OPA : LV_OPA_COVER;

    /* CPU on one line: the average in blue, a muted "/", the top core in its
     * own colour — the same two colours as the graph's two lines. Drawn right
     * to left, so the run ends at R. */
    text_at(layer, FONT_LABEL, L, R, y0 + 134, "CPU", WS_COLOR_MUTED, o, LV_TEXT_ALIGN_LEFT, 1);
    {
        int32_t r = R;
        lv_snprintf(buf, sizeof(buf), "%d%%", h->core);
        text_at(layer, FONT_MID_B, L, r, y0 + 136, buf, WS_COLOR_TOP_CORE, o, LV_TEXT_ALIGN_RIGHT,
                0);
        r -= text_w(FONT_MID_B, buf, 0);
        text_at(layer, FONT_LABEL, L, r, y0 + 136, "  /  ", WS_COLOR_MUTED, o,
                LV_TEXT_ALIGN_RIGHT, 0);
        r -= text_w(FONT_LABEL, "  /  ", 0);
        lv_snprintf(buf, sizeof(buf), "%d%%", h->cpu);
        text_at(layer, FONT_BIG_B, L, r, y0 + 136, buf, WS_COLOR_BLUE, o, LV_TEXT_ALIGN_RIGHT, 0);
    }
    spark(layer, L, y0 + 152, w, 130, s->cpu, s->nhist, WS_COLOR_BLUE, 3, 56, o);
    spark(layer, L, y0 + 152, w, 130, s->core, s->nhist, WS_COLOR_TOP_CORE, 2, 0, o);

    /* RAM: the figure takes the bar's mauve, or the attention colour with the
     * label bold. */
    {
        bool on = h->ram_att != HC_ATT_NONE;
        lv_color_t rc = on ? att_color(h->ram_att) : WS_COLOR_MAUVE;
        text_at(layer, on ? FONT_LABEL_B : FONT_LABEL, L, R, y0 + 328, "RAM",
                on ? rc : WS_COLOR_MUTED, o, LV_TEXT_ALIGN_LEFT, 1);
        lv_snprintf(buf, sizeof(buf), "%d%%", h->ram_pct);
        text_at(layer, FONT_MID_B, L, R, y0 + 330, buf, rc, o, LV_TEXT_ALIGN_RIGHT, 0);
        bar(layer, L, y0 + 342, w, 12, h->ram_pct, rc, o);
    }

    /* GPU and VRAM; a host without a GPU leaves the band empty, so every row
     * below sits at the same y in every column. */
    if (h->gpu) {
        text_at(layer, FONT_LABEL, L, R, y0 + 398, "GPU", WS_COLOR_MUTED, o, LV_TEXT_ALIGN_LEFT,
                1);
        lv_snprintf(buf, sizeof(buf), "%d%%", h->gpu_pct);
        text_at(layer, FONT_BIG_B, L, R, y0 + 400, buf, WS_COLOR_GREEN, o, LV_TEXT_ALIGN_RIGHT,
                0);
        spark(layer, L, y0 + 412, w, 70, s->gpu, s->nhist, WS_COLOR_GREEN, 4, 38, o);
        text_at(layer, FONT_LABEL, L, R, y0 + 516, "VRAM", WS_COLOR_MUTED, o, LV_TEXT_ALIGN_LEFT,
                1);
        /* Ken, 2026-11-12: the figures take the VRAM bar's peach. */
        text_at(layer, FONT_LABEL, L + 80, R, y0 + 516, h->vram, WS_COLOR_ORANGE, o,
                LV_TEXT_ALIGN_RIGHT, 0);
        bar(layer, L, y0 + 526, w, 10, h->vram_pct, WS_COLOR_ORANGE, o);
    }

    /* One drive row, pinned to the bottom (WI #4544): the fullest drive, and
     * "+N" when others are over the line too. It never moves. */
    if (h->has_disk) {
        const hc_disk_t *d = &h->disk;
        bool on = d->att != HC_ATT_NONE;
        lv_color_t dc = att_color(d->att);
        const lv_font_t *f = on ? FONT_LABEL_B : FONT_LABEL;
        char fig[8];
        lv_snprintf(fig, sizeof(fig), "%d%%", d->pct);
        text_at(layer, FONT_LABEL_B, L, R, y0 + 566, fig, on ? dc : WS_COLOR_FG, o,
                LV_TEXT_ALIGN_RIGHT, 0);
        if (h->disk_more > 0)
            lv_snprintf(buf, sizeof(buf), "%s  +%d", d->name, h->disk_more);
        else
            lv_snprintf(buf, sizeof(buf), "%s", d->name);
        text_at(layer, f, L, R - text_w(FONT_LABEL_B, fig, 0) - 16, y0 + 566, buf,
                on ? dc : WS_COLOR_FG, o, LV_TEXT_ALIGN_LEFT, 0);
        bar(layer, L, y0 + 576, w, 8, d->pct, on ? dc : WS_COLOR_LAVENDER, o);
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
    const int32_t L = x + 18, R = x + HC_CW - 18;
    const int32_t bx = x + 92, bw = HC_CW - 92 - 78;
    char buf[16];

    frame(layer, a, h->att);
    rect(layer, x + 19, y + 26, 16, 16, h->nodata ? WS_COLOR_RED : WS_COLOR_GREEN, LV_OPA_COVER,
         LV_RADIUS_CIRCLE);
    /* Uptime (or NO DATA) right-aligned on the name line; the name gives way. */
    const lv_font_t *sf = h->nodata ? FONT_LABEL_B : FONT_LABEL;
    text_at(layer, sf, L, R, y + 44, h->status, h->nodata ? WS_COLOR_RED : WS_COLOR_FG,
            LV_OPA_COVER, LV_TEXT_ALIGN_RIGHT, 0);
    text_at(layer, FONT_NAME, x + 46, R - text_w(sf, h->status, 0) - 12, y + 44, h->host,
            WS_COLOR_FG, LV_OPA_COVER, LV_TEXT_ALIGN_LEFT, 0);

    const lv_opa_t o = h->nodata ? HC_DIM_OPA : LV_OPA_COVER;
    tile_row_t rows[4];
    int nr = 0;
    rows[nr++] = (tile_row_t){"cpu", h->cpu, WS_COLOR_BLUE, HC_ATT_NONE, h->core};
    rows[nr++] = (tile_row_t){"ram", h->ram_pct, WS_COLOR_MAUVE, h->ram_att, -1};
    if (h->gpu)
        rows[nr++] = (tile_row_t){"gpu", h->gpu_pct, WS_COLOR_GREEN, HC_ATT_NONE, -1};
    char dname[6] = "";
    if (h->has_disk) {
        memcpy(dname, h->disk.name, strnlen(h->disk.name, sizeof(dname) - 1));
        rows[nr++] = (tile_row_t){dname, h->disk.pct, WS_COLOR_LAVENDER, h->disk.att, -1};
    }

    for (int k = 0; k < nr; k++) {
        int32_t yy = y + 82 + k * 30;
        bool on = rows[k].att != HC_ATT_NONE;
        lv_color_t c = att_color(rows[k].att);
        const lv_font_t *f = on ? FONT_LABEL_B : FONT_LABEL;
        text_at(layer, f, L, bx - 4, yy + 7, rows[k].label, on ? c : WS_COLOR_MUTED, o,
                LV_TEXT_ALIGN_LEFT, 0);
        bar(layer, bx, yy - 5, bw, 10, rows[k].pct, on ? c : rows[k].color, o);
        if (rows[k].tick >= 0) {
            int t = rows[k].tick > 100 ? 100 : rows[k].tick;
            rect(layer, bx + bw * t / 100 - 2, yy - 9, 4, 18, WS_COLOR_TOP_CORE, o, 1);
        }
        lv_snprintf(buf, sizeof(buf), "%d%%", rows[k].pct);
        text_at(layer, f, bx + bw, R, yy + 7, buf, on ? c : WS_COLOR_FG, o, LV_TEXT_ALIGN_RIGHT,
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
