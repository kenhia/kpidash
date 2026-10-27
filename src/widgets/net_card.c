/* net_card.c — the 2×1 network-health card, "B · Rail" (WI #4062).
 *
 * Geometry is the design record's (korg:4059, sprints/planning/
 * netcard-mockups.html, cardB): every x/baseline below is the mock-up's own
 * number in the card's 1264×628 space, so the two can be read side by side.
 * Text is placed by BASELINE, as the SVG places it, via put_*().
 *
 * Two font facts shape the text code. The regular Montserrat sizes are
 * LVGL's built-ins and carry ASCII only; the bold ones are generated with
 * Latin-1 and the dashes (fonts/generate.sh). So every line that can carry a
 * "·" or a "—" is a spangroup (rich_t), and rich_set() draws any non-ASCII
 * character in the bold face of the same size — the dot looks the same and
 * nothing renders as a box. The same pass colours the speed tokens
 * (10G teal, 5G sapphire, 1G yellow, DOWN red) wherever they appear.
 *
 * Everything is created once; refresh only re-sets what changed. */
#include "widgets/net_card.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "layout.h"
#include "netstate.h"
#include "widgets/common.h"

/* Mocha shades the shared palette does not carry. */
#define NC_MANTLE lv_color_hex(0x181825)
#define NC_CRUST lv_color_hex(0x11111B)
#define NC_SUBTEXT lv_color_hex(0xA6ADC8)
#define NC_SURFACE2 lv_color_hex(0x585B70)

#define NC_W UNIT_W_N(2) /* 1264 */
#define NC_H UNIT_H      /* 628 */
#define COL_X0 24
#define COL_PITCH 246
#define COL_W 226
#define COL_Y 152
#define COL_H 452
#define SPARK_X 16
#define SPARK_Y 160
#define SPARK_W (COL_W - 32)
#define SPARK_H 100

/* ---- Rich text: coloured tokens, non-ASCII in the bold face ---- */

typedef struct {
    lv_obj_t *sg;
    char last[200];
    uint32_t last_color;
    const lv_font_t *last_font;
} rich_t;

static lv_color_t token_color(const char *t, size_t n, bool *hit) {
    *hit = true;
    if (n == 3 && strncmp(t, "10G", 3) == 0)
        return WS_COLOR_TEAL;
    if (n == 2 && strncmp(t, "5G", 2) == 0)
        return WS_COLOR_SAPPHIRE;
    if (n == 2 && strncmp(t, "1G", 2) == 0)
        return WS_COLOR_YELLOW;
    if (n == 4 && strncmp(t, "DOWN", 4) == 0)
        return WS_COLOR_RED;
    *hit = false;
    return WS_COLOR_FG;
}

static bool is_word(char c) {
    return (c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');
}

static void span_add(lv_obj_t *sg, const char *s, size_t n, const lv_font_t *f, lv_color_t c) {
    if (n == 0)
        return;
    char buf[200];
    if (n >= sizeof(buf))
        n = sizeof(buf) - 1;
    memcpy(buf, s, n);
    buf[n] = '\0';
    lv_span_t *sp = lv_spangroup_new_span(sg);
    lv_span_set_text(sp, buf);
    lv_style_set_text_font(lv_span_get_style(sp), f);
    lv_style_set_text_color(lv_span_get_style(sp), c);
}

static void rich_set(rich_t *r, const char *text, const lv_font_t *font, const lv_font_t *bold,
                     lv_color_t color, bool tokens) {
    uint32_t ckey = lv_color_to_u32(color);
    if (strcmp(r->last, text) == 0 && r->last_color == ckey && r->last_font == font)
        return;
    snprintf(r->last, sizeof(r->last), "%s", text);
    r->last_color = ckey;
    r->last_font = font;

    while (lv_spangroup_get_span_count(r->sg) > 0)
        lv_spangroup_delete_span(r->sg, lv_spangroup_get_child(r->sg, 0));

    const char *run = text;
    const char *p = text;
    while (*p) {
        if ((unsigned char)*p >= 0x80) {
            span_add(r->sg, run, (size_t)(p - run), font, color);
            const char *q = p + 1;
            while ((*q & 0xC0) == 0x80)
                q++;
            span_add(r->sg, p, (size_t)(q - p), bold, color);
            p = run = q;
            continue;
        }
        if (tokens && (p == text || !is_word(p[-1]))) {
            size_t n = 0;
            while (is_word(p[n]))
                n++;
            bool hit = false;
            lv_color_t tc = token_color(p, n, &hit);
            if (hit) {
                span_add(r->sg, run, (size_t)(p - run), font, color);
                span_add(r->sg, p, n, font, tc);
                p = run = p + n;
                continue;
            }
        }
        p++;
    }
    span_add(r->sg, run, (size_t)(p - run), font, color);
    lv_spangroup_refr_mode(r->sg);
}

/* ---- Placement by baseline ---- */

static int32_t top_for(const lv_font_t *f, int32_t baseline) {
    return baseline - (f->line_height - f->base_line);
}

static void bare(lv_obj_t *o) {
    lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_CLICKABLE);
}

/* align: LV_TEXT_ALIGN_LEFT anchors at x; RIGHT ends at x. */
static lv_obj_t *put_label(lv_obj_t *parent, const lv_font_t *f, int32_t x, int32_t baseline,
                           lv_text_align_t align, int32_t box_w) {
    lv_obj_t *l = lv_label_create(parent);
    bare(l);
    lv_obj_set_style_text_font(l, f, 0);
    lv_obj_set_style_text_align(l, align, 0);
    lv_label_set_long_mode(l, LV_LABEL_LONG_CLIP);
    lv_obj_set_size(l, box_w, f->line_height);
    lv_obj_set_pos(l, align == LV_TEXT_ALIGN_RIGHT ? x - box_w : x, top_for(f, baseline));
    lv_label_set_text(l, "");
    return l;
}

static void put_rich(rich_t *r, lv_obj_t *parent, const lv_font_t *f, int32_t x, int32_t baseline,
                     lv_text_align_t align, int32_t box_w) {
    r->sg = lv_spangroup_create(parent);
    bare(r->sg);
    r->last[0] = '\x01'; /* never matches: the first rich_set always draws */
    lv_spangroup_set_mode(r->sg, LV_SPAN_MODE_FIXED);
    lv_spangroup_set_overflow(r->sg, LV_SPAN_OVERFLOW_CLIP);
    lv_spangroup_set_max_lines(r->sg, 1);
    lv_spangroup_set_align(r->sg, align);
    /* Room for a larger bold glyph than the base font's line. */
    lv_obj_set_size(r->sg, box_w, f->line_height + 4);
    lv_obj_set_pos(r->sg, align == LV_TEXT_ALIGN_RIGHT ? x - box_w : x, top_for(f, baseline));
}

static void set_text(lv_obj_t *l, const char *t) {
    if (strcmp(lv_label_get_text(l), t) != 0)
        lv_label_set_text(l, t);
}

static void set_color(lv_obj_t *l, lv_color_t c) {
    lv_obj_set_style_text_color(l, c, 0);
}

static lv_obj_t *rect(lv_obj_t *parent, int32_t x, int32_t y, int32_t w, int32_t h, int32_t r) {
    lv_obj_t *o = lv_obj_create(parent);
    lv_obj_remove_style_all(o);
    bare(o);
    lv_obj_set_pos(o, x, y);
    lv_obj_set_size(o, w, h);
    lv_obj_set_style_radius(o, r, 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    return o;
}

/* ---- Hatch: the stale / no-feed fill (8 px, 45°) ---- */

static uint32_t g_hatch_px[8 * 8];
static lv_image_dsc_t g_hatch;

static void hatch_init(void) {
    for (int y = 0; y < 8; y++)
        for (int x = 0; x < 8; x++)
            g_hatch_px[y * 8 + x] = ((x + y) % 8 < 3) ? 0xFF585B70u : 0xFF313244u;
    memset(&g_hatch, 0, sizeof(g_hatch));
    g_hatch.header.magic = LV_IMAGE_HEADER_MAGIC;
    g_hatch.header.cf = LV_COLOR_FORMAT_ARGB8888;
    g_hatch.header.w = 8;
    g_hatch.header.h = 8;
    g_hatch.header.stride = 8 * 4;
    g_hatch.data_size = sizeof(g_hatch_px);
    g_hatch.data = (const uint8_t *)g_hatch_px;
}

/* ---- Sparkline ---- */

typedef struct {
    float v[NET_HIST_N + 1];
    int n;
    float drop_x[NET_DROPS_MAX]; /* 0..1 across the hour */
    int nd;
    lv_color_t color;
} spark_t;

static void spark_draw_cb(lv_event_t *e) {
    lv_obj_t *obj = lv_event_get_target(e);
    const spark_t *s = lv_obj_get_user_data(obj);
    lv_layer_t *layer = lv_event_get_layer(e);
    if (!s || !layer)
        return;
    lv_area_t a;
    lv_obj_get_coords(obj, &a);
    const int32_t w = lv_area_get_width(&a) - 1, h = lv_area_get_height(&a) - 1;
    const int32_t base = a.y1 + h;

    if (s->n >= 1) {
        float mx = 0;
        for (int i = 0; i < s->n; i++)
            mx = s->v[i] > mx ? s->v[i] : mx;
        mx = mx > 0.5f ? mx * 1.1f : 1.0f;
        float px[NET_HIST_N + 1], py[NET_HIST_N + 1];
        /* Right-anchored on a fixed hour: the newest point is "now" at the
         * right edge, one step per minute, so a young history fills from the
         * right and x means the same time the drop ticks do. */
        const float step = (float)w / (float)NET_HIST_N;
        for (int i = 0; i < s->n; i++) {
            px[i] = (float)(a.x1 + w) - (float)(s->n - 1 - i) * step;
            py[i] = (float)base - s->v[i] / mx * (float)h;
        }

        /* Area fill: one 2 px column per step, so there are no seams. */
        lv_draw_rect_dsc_t fill;
        lv_draw_rect_dsc_init(&fill);
        fill.bg_color = s->color;
        fill.bg_opa = (lv_opa_t)46; /* the mock-up's .18 */
        int seg = 0;
        for (int32_t x = (int32_t)px[0]; s->n >= 2 && x <= a.x1 + w; x += 2) {
            while (seg < s->n - 2 && (float)x > px[seg + 1])
                seg++;
            float t = (px[seg + 1] > px[seg]) ? ((float)x - px[seg]) / (px[seg + 1] - px[seg]) : 0;
            if (t < 0)
                t = 0;
            if (t > 1)
                t = 1;
            int32_t y = (int32_t)lroundf(py[seg] + t * (py[seg + 1] - py[seg]));
            lv_area_t col = {x, y, x + 1 > a.x1 + w ? a.x1 + w : x + 1, base};
            if (col.y1 <= col.y2)
                lv_draw_rect(layer, &fill, &col);
        }

        lv_draw_line_dsc_t ln;
        lv_draw_line_dsc_init(&ln);
        ln.color = s->color;
        ln.width = 3;
        ln.round_start = 1;
        ln.round_end = 1;
        for (int i = 0; i + 1 < s->n; i++) {
            ln.p1.x = px[i];
            ln.p1.y = py[i];
            ln.p2.x = px[i + 1];
            ln.p2.y = py[i + 1];
            lv_draw_line(layer, &ln);
        }

        lv_draw_rect_dsc_t dot;
        lv_draw_rect_dsc_init(&dot);
        dot.bg_color = s->color;
        dot.radius = LV_RADIUS_CIRCLE;
        int32_t ex = (int32_t)px[s->n - 1], ey = (int32_t)py[s->n - 1];
        lv_area_t d = {ex - 4, ey - 4, ex + 4, ey + 4};
        lv_draw_rect(layer, &dot, &d);
    }

    /* Link drops: a red tick and a marker at the moment of each drop. */
    for (int i = 0; i < s->nd; i++) {
        float x = (float)a.x1 + s->drop_x[i] * (float)w;
        lv_draw_line_dsc_t tk;
        lv_draw_line_dsc_init(&tk);
        tk.color = WS_COLOR_RED;
        tk.width = 3;
        tk.p1.x = x;
        tk.p1.y = a.y1 + 18;
        tk.p2.x = x;
        tk.p2.y = base;
        lv_draw_line(layer, &tk);
        lv_draw_triangle_dsc_t tri;
        lv_draw_triangle_dsc_init(&tri);
        tri.bg_color = WS_COLOR_RED;
        tri.bg_opa = LV_OPA_COVER;
        tri.p[0].x = x - 6;
        tri.p[0].y = a.y1 + 12;
        tri.p[1].x = x + 6;
        tri.p[1].y = a.y1 + 12;
        tri.p[2].x = x;
        tri.p[2].y = a.y1 + 22;
        lv_draw_triangle(layer, &tri);
    }
}

/* ---- The card ---- */

typedef struct {
    lv_obj_t *col, *hatch, *frame, *band, *tick, *name, *nic, *num, *mbps, *pill[3], *pill_lbl[3],
        *spark, *foot_r;
    rich_t sub, unit, lfc, rx, foot_l;
    spark_t sd;
} netcol_t;

static struct {
    lv_obj_t *root, *rail, *tile[3], *tile_lbl[3], *tile_val[3];
    rich_t verdict, why;
    netcol_t cols[NET_HOSTS_MAX];
} g;

static const char *const PILL_TEXT[3] = {"UDM", "PEERS", "WAN"};
static const char *const TILE_TEXT[3] = {"UDM", "WAN", "UPLINK"};

static void col_create(netcol_t *c, int i) {
    int32_t x = COL_X0 + i * COL_PITCH;
    c->tick = rect(g.root, x + COL_W / 2 - 3, 134, 6, 18, 0);

    c->col = rect(g.root, x, COL_Y, COL_W, COL_H, 10);
    lv_obj_set_style_clip_corner(c->col, true, 0);
    /* The hatch is a child layer, not c->col's own bg image: clip_corner
     * clips children to the radius but not the object's own image. */
    c->hatch = rect(c->col, 0, 0, COL_W, COL_H, 0);
    lv_obj_set_style_bg_opa(c->hatch, LV_OPA_TRANSP, 0);
    lv_obj_set_style_bg_image_src(c->hatch, &g_hatch, 0);
    lv_obj_set_style_bg_image_tiled(c->hatch, true, 0);
    /* The frame is its own border-only object, so a 3 px attention frame
     * never moves the column's contents the way a border on c->col would. */
    c->frame = rect(c->col, 0, 0, COL_W, COL_H, 10);
    lv_obj_set_style_bg_opa(c->frame, LV_OPA_TRANSP, 0);
    c->band = rect(c->col, 0, 0, COL_W, 6, 3);

    c->name = put_label(c->col, &lv_font_montserrat_bold_20, 16, 40, LV_TEXT_ALIGN_LEFT, 130);
    set_color(c->name, WS_COLOR_FG);
    c->nic = put_label(c->col, WS_FONT_TINY, COL_W - 16, 40, LV_TEXT_ALIGN_RIGHT, 80);
    set_color(c->nic, WS_COLOR_MUTED);
    c->num = put_label(c->col, &lv_font_montserrat_bold_48, 16, 104, LV_TEXT_ALIGN_LEFT, 200);
    put_rich(&c->sub, c->col, &lv_font_montserrat_bold_16, 16, 138, LV_TEXT_ALIGN_LEFT, 200);

    c->spark = lv_obj_create(c->col);
    lv_obj_remove_style_all(c->spark);
    bare(c->spark);
    lv_obj_set_pos(c->spark, SPARK_X, SPARK_Y);
    lv_obj_set_size(c->spark, SPARK_W, SPARK_H);
    lv_obj_set_user_data(c->spark, &c->sd);
    lv_obj_add_event_cb(c->spark, spark_draw_cb, LV_EVENT_DRAW_MAIN_END, NULL);

    put_rich(&c->unit, c->col, WS_FONT_TINY, 16, 280, LV_TEXT_ALIGN_LEFT, 120);
    rich_set(&c->unit, "Mb/s · 60 min", WS_FONT_TINY, &lv_font_montserrat_bold_14, WS_COLOR_MUTED,
             false);
    c->mbps = put_label(c->col, &lv_font_montserrat_bold_14, COL_W - 16, 280, LV_TEXT_ALIGN_RIGHT,
                        70);
    set_color(c->mbps, WS_COLOR_FG);

    put_rich(&c->lfc, c->col, WS_FONT_SMALL, 16, 318, LV_TEXT_ALIGN_LEFT, 120);
    put_rich(&c->rx, c->col, WS_FONT_SMALL, COL_W - 16, 318, LV_TEXT_ALIGN_RIGHT, 120);

    for (int j = 0; j < 3; j++) {
        c->pill[j] = rect(c->col, 16 + j * 66, 348, 60, 26, LV_RADIUS_CIRCLE);
        c->pill_lbl[j] = lv_label_create(c->pill[j]);
        bare(c->pill_lbl[j]);
        lv_obj_set_style_text_font(c->pill_lbl[j], &lv_font_montserrat_bold_14, 0);
        lv_label_set_text_static(c->pill_lbl[j], PILL_TEXT[j]);
        lv_obj_center(c->pill_lbl[j]);
    }

    put_rich(&c->foot_l, c->col, WS_FONT_TINY, 16, 418, LV_TEXT_ALIGN_LEFT, 120);
    c->foot_r = put_label(c->col, WS_FONT_TINY, COL_W - 16, 418, LV_TEXT_ALIGN_RIGHT, 80);
    set_color(c->foot_r, WS_COLOR_MUTED);
}

lv_obj_t *net_card_create(lv_obj_t *parent) {
    hatch_init();
    memset(&g, 0, sizeof(g));

    /* Fortune's frame, which this card replaces: Mantle, Surface1, radius 8. */
    g.root = lv_obj_create(parent);
    bare(g.root);
    lv_obj_set_size(g.root, NC_W, NC_H);
    lv_obj_set_style_bg_color(g.root, NC_MANTLE, 0);
    lv_obj_set_style_bg_opa(g.root, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(g.root, 8, 0);
    lv_obj_set_style_border_color(g.root, WS_COLOR_SURFACE1, 0);
    lv_obj_set_style_border_width(g.root, 1, 0);
    lv_obj_set_style_pad_all(g.root, 0, 0);

    put_rich(&g.verdict, g.root, &lv_font_montserrat_bold_48, 24, 72, LV_TEXT_ALIGN_LEFT, 720);
    put_rich(&g.why, g.root, WS_FONT_SMALL, 26, 102, LV_TEXT_ALIGN_LEFT, 720);

    for (int i = 0; i < 3; i++) {
        int32_t x = 760 + i * 164;
        g.tile[i] = rect(g.root, x, 28, 150, 76, 8);
        lv_obj_set_style_bg_color(g.tile[i], WS_COLOR_BG, 0);
        lv_obj_set_style_border_color(g.tile[i], WS_COLOR_SURFACE1, 0);
        lv_obj_set_style_border_width(g.tile[i], 1, 0);
        /* Tile children are placed in the tile's own space, less its border. */
        g.tile_lbl[i] = put_label(g.tile[i], WS_FONT_TINY, 11, 52 - 28 - 1, LV_TEXT_ALIGN_LEFT, 120);
        set_color(g.tile_lbl[i], WS_COLOR_MUTED);
        lv_label_set_text_static(g.tile_lbl[i], TILE_TEXT[i]);
        g.tile_val[i] =
            put_label(g.tile[i], &lv_font_montserrat_bold_24, 11, 88 - 28 - 1, LV_TEXT_ALIGN_LEFT,
                      130);
    }

    g.rail = rect(g.root, 24, 126, 1216, 8, 4);

    for (int i = 0; i < NET_HOSTS_MAX; i++)
        col_create(&g.cols[i], i);
    return g.root;
}

/* ---- Refresh ---- */

static lv_color_t leg_color(net_leg_t leg) {
    switch (leg) {
    case NET_LEG_10G:
        return WS_COLOR_TEAL;
    case NET_LEG_5G:
        return WS_COLOR_SAPPHIRE;
    case NET_LEG_1G:
        return WS_COLOR_YELLOW;
    case NET_LEG_DOWN:
    default:
        return WS_COLOR_RED;
    }
}

static lv_color_t level_color(net_level_t l) {
    switch (l) {
    case NET_LEVEL_OK:
        return WS_COLOR_TEAL;
    case NET_LEVEL_WARN:
        return WS_COLOR_YELLOW;
    case NET_LEVEL_CRIT:
        return WS_COLOR_RED;
    case NET_LEVEL_NONE:
    default:
        return WS_COLOR_MUTED;
    }
}

static void col_update(netcol_t *c, const net_host_t *h, double now) {
    const net_record_t *r = &h->rec;
    const bool live = h->state == NET_COL_LIVE;
    const bool grey = !live;
    const bool nofeed = h->state == NET_COL_NOFEED;
    const net_attn_t at = net_column_attn(h);
    const bool attn = live && at != NET_ATTN_NONE;
    const lv_color_t at_c = at == NET_ATTN_CRIT ? WS_COLOR_RED : WS_COLOR_YELLOW;
    const lv_color_t legc = grey ? WS_COLOR_MUTED : leg_color(r->leg);
    const lv_color_t band_c = grey ? NC_SURFACE2 : (attn ? at_c : legc);
    char buf[96];

    lv_obj_set_style_bg_color(c->tick, band_c, 0);
    lv_obj_set_style_bg_color(c->band, band_c, 0);
    lv_obj_set_height(c->band, attn ? 10 : 6);
    lv_obj_set_style_border_color(c->frame, grey ? NC_SURFACE2 : (attn ? at_c : WS_COLOR_SURFACE1),
                                  0);
    lv_obj_set_style_border_width(c->frame, attn ? 3 : 1, 0);
    lv_obj_set_style_bg_color(c->col, WS_COLOR_BG, 0);
    if (grey)
        lv_obj_remove_flag(c->hatch, LV_OBJ_FLAG_HIDDEN);
    else
        lv_obj_add_flag(c->hatch, LV_OBJ_FLAG_HIDDEN);

    set_text(c->name, h->host);
    set_text(c->nic, nofeed ? "" : r->nic);
    set_color(c->num, legc);
    set_text(c->num, grey ? "—" : net_leg_text(r->leg));

    bool bold;
    net_subtitle(h, now, buf, sizeof(buf), &bold);
    lv_color_t sub_c = NC_SUBTEXT;
    if (nofeed)
        sub_c = WS_COLOR_MUTED;
    else if (h->state == NET_COL_STALE)
        sub_c = WS_COLOR_FG;
    else if (bold)
        sub_c = at == NET_ATTN_CRIT ? WS_COLOR_RED : WS_COLOR_YELLOW;
    rich_set(&c->sub, buf, bold ? &lv_font_montserrat_bold_16 : WS_FONT_SMALL,
             &lv_font_montserrat_bold_16, sub_c, live);

    /* Sparkline: the per-minute history plus the minute in progress. */
    spark_t *s = &c->sd;
    s->n = 0;
    for (int i = 0; i < h->hist_n; i++)
        s->v[s->n++] = h->hist[i];
    if (h->acc_n > 0)
        s->v[s->n++] = (float)(h->acc_sum / h->acc_n);
    s->nd = 0;
    if (live)
        for (int i = 0; i < r->ndrops; i++) {
            double f = 1.0 - (now - r->drops[i]) / NET_WINDOW_S;
            if (f >= 0 && f <= 1)
                s->drop_x[s->nd++] = (float)f;
        }
    s->color = grey ? NC_SURFACE2 : legc;
    lv_obj_invalidate(c->spark);

    if (nofeed)
        set_text(c->mbps, "—");
    else {
        snprintf(buf, sizeof(buf), "%.0f", r->bytes_per_s * 8.0 / 1e6);
        set_text(c->mbps, buf);
    }

    const bool lfc_warn = live && (r->lfc_delta_24h > 0 || r->ndrops > 0);
    if (nofeed)
        snprintf(buf, sizeof(buf), "LFC —");
    else if (!r->bond)
        snprintf(buf, sizeof(buf), "no bond");
    else if (r->lfc < 0)
        snprintf(buf, sizeof(buf), "LFC —");
    else if (r->lfc_delta_24h > 0)
        snprintf(buf, sizeof(buf), "LFC %ld (+%ld)", r->lfc, r->lfc_delta_24h);
    else
        snprintf(buf, sizeof(buf), "LFC %ld", r->lfc);
    rich_set(&c->lfc, buf, lfc_warn ? &lv_font_montserrat_bold_16 : WS_FONT_SMALL,
             &lv_font_montserrat_bold_16, lfc_warn ? WS_COLOR_YELLOW : NC_SUBTEXT, false);

    const bool rx_warn = live && r->rx_errors_delta_24h > 0;
    net_rx_text(h, buf, sizeof(buf));
    rich_set(&c->rx, buf, rx_warn ? &lv_font_montserrat_bold_16 : WS_FONT_SMALL,
             &lv_font_montserrat_bold_16, rx_warn ? WS_COLOR_YELLOW : NC_SUBTEXT, false);

    const bool reach[3] = {r->reach_udm, r->reach_peers, r->reach_wan};
    for (int j = 0; j < 3; j++) {
        lv_color_t fill = grey ? WS_COLOR_SURFACE1 : (reach[j] ? WS_COLOR_TEAL : WS_COLOR_RED);
        lv_obj_set_style_bg_color(c->pill[j], fill, 0);
        set_color(c->pill_lbl[j], grey ? WS_COLOR_FG : NC_CRUST);
    }

    if (grey)
        snprintf(buf, sizeof(buf), "—");
    else if (r->reach_wan && r->rtt_wan >= 0)
        snprintf(buf, sizeof(buf), "WAN %.0f ms", r->rtt_wan);
    else if (r->reach_udm && r->rtt_udm >= 0)
        snprintf(buf, sizeof(buf), "UDM %.1f ms", r->rtt_udm);
    else
        snprintf(buf, sizeof(buf), "%s", r->reach_peers ? "peers only" : "no path");
    rich_set(&c->foot_l, buf, WS_FONT_TINY, &lv_font_montserrat_bold_14, WS_COLOR_MUTED, false);

    if (nofeed)
        set_text(c->foot_r, "");
    else {
        double age = now - r->ts;
        snprintf(buf, sizeof(buf), "%.0f s ago", age < 0 ? 0.0 : age);
        set_text(c->foot_r, buf);
    }
}

void net_card_update(lv_obj_t *card, double now) {
    if (!card || card != g.root)
        return;
    net_summary_t s;
    net_derive(now, &s);
    const lv_color_t lc = level_color(s.level);

    rich_set(&g.verdict, s.verdict, &lv_font_montserrat_bold_48, &lv_font_montserrat_bold_48, lc,
             true);
    rich_set(&g.why, s.why, WS_FONT_SMALL, &lv_font_montserrat_bold_16, NC_SUBTEXT, true);
    lv_obj_set_style_bg_color(g.rail, lc, 0);

    char buf[32];
    const bool none = s.level == NET_LEVEL_NONE;
    /* UDM / WAN RTT tiles: medians over the live hosts that reach them. */
    const double ms[2] = {s.udm_ms, s.wan_ms};
    const bool up[3] = {s.udm_up, s.wan_up, s.uplink_up};
    for (int i = 0; i < 3; i++) {
        if (none) {
            set_text(g.tile_val[i], "—");
            set_color(g.tile_val[i], WS_COLOR_MUTED);
            continue;
        }
        if (!up[i])
            snprintf(buf, sizeof(buf), "%s", i == 2 ? "LOST" : "DOWN");
        else if (i == 2)
            snprintf(buf, sizeof(buf), "1G"); /* that link's normal speed: neutral, not yellow */
        else if (ms[i] < 0)
            snprintf(buf, sizeof(buf), "up");
        else if (ms[i] < 10)
            snprintf(buf, sizeof(buf), "%.1f ms", ms[i]);
        else
            snprintf(buf, sizeof(buf), "%.0f ms", ms[i]);
        set_text(g.tile_val[i], buf);
        set_color(g.tile_val[i], up[i] ? WS_COLOR_FG : WS_COLOR_RED);
    }

    const int n = net_host_count();
    for (int i = 0; i < NET_HOSTS_MAX; i++) {
        netcol_t *c = &g.cols[i];
        if (i >= n) {
            lv_obj_add_flag(c->col, LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(c->tick, LV_OBJ_FLAG_HIDDEN);
            continue;
        }
        lv_obj_remove_flag(c->col, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(c->tick, LV_OBJ_FLAG_HIDDEN);
        col_update(c, net_host_at(i), now);
    }
}
