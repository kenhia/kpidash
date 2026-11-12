/* nas_card.c — the NAS big panel, "A · Rail" in mauve (sprint 033, WI #4566;
 * first built for WI #3956). See nas_card.h.
 *
 * Geometry is the mock-up's (sprints/planning/nas-card-mockups.html, vA):
 * every x/baseline below is the mock-up's own number in its 1264×628 space,
 * so the two can be read side by side. The card itself is the cell less
 * CELL_PAD on every side (the mock-up's frame is drawn 4 px in), so M()
 * shifts a mock-up coordinate into the card. Text is placed by BASELINE, as
 * the SVG places it.
 *
 * The 10G card's skeleton in the NAS's own colour: mauve is "healthy" here
 * the way teal is there, standby is grey, yellow and red keep their meaning.
 * The frame is a neutral line and turns yellow or red only when something is;
 * it is never green. Nothing is under 20 px.
 *
 * Font fact, as on the 10G card: the regular Montserrat sizes are LVGL's
 * built-ins and carry ASCII only, the bold ones are generated with Latin-1.
 * A regular line that can carry a "·" is a spangroup (rich_t) that draws
 * every non-ASCII character in the bold face of the same size.
 *
 * Every fill is opaque — the cold tiles' blue is mixed ahead of time — because
 * on the Pi a translucent fill reads back every pixel it covers (sprint 030).
 * Everything is created once; refresh only re-sets what changed. */
#include "widgets/nas_card.h"

#include <stdio.h>
#include <string.h>

#include "layout.h"
#include "nasstate.h"
#include "protocol.h"
#include "registry.h"
#include "widgets/common.h"

#define NAS_W (UNIT_W_N(2) - 2 * CELL_PAD) /* 1256 */
#define NAS_H (UNIT_H - 2 * CELL_PAD)      /* 620 */
#define M(v) ((v) - CELL_PAD)              /* mock-up space -> card space */

#define TILES 8
#define TILE_W 139
#define TILE_Y 240
#define TILE_H 356
#define TILE_PAD 14

/* Mocha shades the shared palette does not carry. */
#define C_MANTLE lv_color_hex(0x181825)
#define C_CRUST lv_color_hex(0x11111B)
#define C_SUBTEXT lv_color_hex(0xA6ADC8)
/* Base with 12 % Mocha blue: the cold disks' hint, pre-mixed so it stays
 * opaque. */
#define C_COLD lv_color_hex(0x2B3046)

/* ---- Small helpers: set only what changed ---- */

static void bare(lv_obj_t *o) {
    lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_CLICKABLE);
}

static void set_text(lv_obj_t *l, const char *t) {
    if (strcmp(lv_label_get_text(l), t) != 0)
        lv_label_set_text(l, t);
}

static void set_color(lv_obj_t *l, lv_color_t c) {
    if (!lv_color_eq(lv_obj_get_style_text_color(l, 0), c))
        lv_obj_set_style_text_color(l, c, 0);
}

static void set_bg(lv_obj_t *o, lv_color_t c) {
    if (!lv_color_eq(lv_obj_get_style_bg_color(o, 0), c))
        lv_obj_set_style_bg_color(o, c, 0);
}

static void set_font(lv_obj_t *l, const lv_font_t *f) {
    if (lv_obj_get_style_text_font(l, 0) != f)
        lv_obj_set_style_text_font(l, f, 0);
}

static void set_hidden(lv_obj_t *o, bool hidden) {
    if (hidden != lv_obj_has_flag(o, LV_OBJ_FLAG_HIDDEN)) {
        if (hidden)
            lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN);
        else
            lv_obj_remove_flag(o, LV_OBJ_FLAG_HIDDEN);
    }
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

/* ---- Placement by baseline ---- */

static int32_t top_for(const lv_font_t *f, int32_t baseline) {
    return baseline - (f->line_height - f->base_line);
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

/* ---- Rich text: a regular line whose non-ASCII is drawn in bold ---- */

typedef struct {
    lv_obj_t *sg;
    char last[160];
    uint32_t last_color;
} rich_t;

static void span_add(lv_obj_t *sg, const char *s, size_t n, const lv_font_t *f, lv_color_t c) {
    if (n == 0)
        return;
    char buf[160];
    if (n >= sizeof(buf))
        n = sizeof(buf) - 1;
    memcpy(buf, s, n);
    buf[n] = '\0';
    lv_span_t *sp = lv_spangroup_new_span(sg);
    lv_span_set_text(sp, buf);
    lv_style_set_text_font(lv_span_get_style(sp), f);
    lv_style_set_text_color(lv_span_get_style(sp), c);
}

/* Always the regular 20 with the bold 20 for "·" and "°": the only rich size
 * this card uses. */
static void rich_set(rich_t *r, const char *text, lv_color_t color) {
    uint32_t ckey = lv_color_to_u32(color);
    if (strcmp(r->last, text) == 0 && r->last_color == ckey)
        return;
    snprintf(r->last, sizeof(r->last), "%s", text);
    r->last_color = ckey;

    while (lv_spangroup_get_span_count(r->sg) > 0)
        lv_spangroup_delete_span(r->sg, lv_spangroup_get_child(r->sg, 0));
    const char *run = text, *p = text;
    while (*p) {
        if ((unsigned char)*p >= 0x80) {
            span_add(r->sg, run, (size_t)(p - run), &lv_font_montserrat_20, color);
            const char *q = p + 1;
            while ((*q & 0xC0) == 0x80)
                q++;
            span_add(r->sg, p, (size_t)(q - p), &lv_font_montserrat_bold_20, color);
            p = run = q;
            continue;
        }
        p++;
    }
    span_add(r->sg, run, (size_t)(p - run), &lv_font_montserrat_20, color);
    lv_spangroup_refr_mode(r->sg);
}

static void put_rich(rich_t *r, lv_obj_t *parent, int32_t x, int32_t baseline,
                     lv_text_align_t align, int32_t box_w) {
    const lv_font_t *f = &lv_font_montserrat_20;
    r->sg = lv_spangroup_create(parent);
    bare(r->sg);
    r->last[0] = '\x01'; /* never matches: the first rich_set always draws */
    lv_spangroup_set_mode(r->sg, LV_SPAN_MODE_FIXED);
    lv_spangroup_set_overflow(r->sg, LV_SPAN_OVERFLOW_CLIP);
    lv_spangroup_set_max_lines(r->sg, 1);
    lv_spangroup_set_align(r->sg, align);
    lv_obj_set_size(r->sg, box_w, f->line_height + 4);
    lv_obj_set_pos(r->sg, align == LV_TEXT_ALIGN_RIGHT ? x - box_w : x, top_for(f, baseline));
}

/* ---- Bars: an opaque track and a fill at least as wide as it is tall ---- */

typedef struct {
    lv_obj_t *track, *fill;
    int32_t w, h;
} bar_t;

static void bar_create(bar_t *b, lv_obj_t *parent, int32_t x, int32_t y, int32_t w, int32_t h) {
    b->w = w;
    b->h = h;
    b->track = rect(parent, x, y, w, h, h / 2);
    lv_obj_set_style_bg_color(b->track, WS_COLOR_SURFACE0, 0);
    b->fill = rect(parent, x, y, h, h, h / 2);
}

static void bar_set(bar_t *b, double pct, lv_color_t c) {
    if (pct > 100)
        pct = 100;
    set_hidden(b->fill, !(pct > 0));
    if (!(pct > 0))
        return;
    int32_t w = (int32_t)(b->w * pct / 100.0 + 0.5);
    if (w < b->h)
        w = b->h;
    if (lv_obj_get_style_width(b->fill, 0) != w)
        lv_obj_set_width(b->fill, w);
    set_bg(b->fill, c);
}

/* ---- The card ---- */

typedef struct {
    lv_obj_t *tick, *tile, *edge, *use, *dev, *kind, *temp, *power, *extra, *pill, *pill_lbl;
    bar_t tbar;
} tile_t;

static struct {
    lv_obj_t *root, *frame, *head, *box[3], *box_val[3], *pool_lbl, *root_lbl, *rail;
    rich_t sub, pool_txt, root_txt;
    bar_t pool_bar, root_bar;
    tile_t tiles[TILES];
} g;

static const char *const BOX_TEXT[3] = {"POOL", "HOTTEST", "DATA AGE"};

static void tile_create(tile_t *t, int i) {
    /* The mock-up's own spacing: eight tiles across 1200 px from x=32. */
    int32_t x = M((int32_t)(32 + i * (1200.0 - TILE_W) / 7.0 + 0.5));
    t->tick = rect(g.root, x + TILE_W / 2 - 3, M(225), 6, 18, 0);

    t->tile = rect(g.root, x, M(TILE_Y), TILE_W, TILE_H, 8);
    lv_obj_set_style_clip_corner(t->tile, true, 0);
    lv_obj_set_style_bg_color(t->tile, WS_COLOR_BG, 0);
    t->edge = rect(t->tile, 0, 0, TILE_W, 6, 3);

    /* Tile children are in the tile's own space: baseline less TILE_Y. */
    const int32_t w = TILE_W - 2 * TILE_PAD;
    t->use = put_label(t->tile, &lv_font_montserrat_bold_20, TILE_PAD, 278 - TILE_Y,
                       LV_TEXT_ALIGN_LEFT, w);
    t->dev = put_label(t->tile, &lv_font_montserrat_bold_24, TILE_PAD, 318 - TILE_Y,
                       LV_TEXT_ALIGN_LEFT, w);
    t->kind = put_label(t->tile, &lv_font_montserrat_20, TILE_PAD, 346 - TILE_Y,
                        LV_TEXT_ALIGN_LEFT, w);
    set_color(t->kind, WS_COLOR_MUTED);
    /* 44 bold in the mock-up; 48 is the nearest generated size. */
    t->temp = put_label(t->tile, &lv_font_montserrat_bold_48, TILE_PAD, 410 - TILE_Y,
                        LV_TEXT_ALIGN_LEFT, w);
    bar_create(&t->tbar, t->tile, TILE_PAD, 424 - TILE_Y, w, 8);
    t->power = put_label(t->tile, &lv_font_montserrat_20, TILE_PAD, 468 - TILE_Y,
                         LV_TEXT_ALIGN_LEFT, w);
    t->extra = put_label(t->tile, &lv_font_montserrat_20, TILE_PAD, 496 - TILE_Y,
                         LV_TEXT_ALIGN_LEFT, w);
    t->pill = rect(t->tile, TILE_PAD, 540 - TILE_Y, w, 36, 18);
    t->pill_lbl = lv_label_create(t->pill);
    bare(t->pill_lbl);
    lv_obj_set_style_text_font(t->pill_lbl, &lv_font_montserrat_bold_20, 0);
    lv_label_set_text(t->pill_lbl, "");
    lv_obj_center(t->pill_lbl);
}

lv_obj_t *nas_card_create(lv_obj_t *parent) {
    memset(&g, 0, sizeof(g));
    g.root = lv_obj_create(parent);
    bare(g.root);
    lv_obj_set_size(g.root, NAS_W, NAS_H);
    lv_obj_set_style_bg_color(g.root, C_MANTLE, 0);
    lv_obj_set_style_bg_opa(g.root, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(g.root, 8, 0);
    lv_obj_set_style_border_width(g.root, 0, 0);
    lv_obj_set_style_pad_all(g.root, 0, 0);
    /* The frame is its own border-only object, so a 3 px attention frame
     * never moves the card's contents the way a border on g.root would. */
    g.frame = rect(g.root, 0, 0, NAS_W, NAS_H, 8);
    lv_obj_set_style_bg_opa(g.frame, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_color(g.frame, WS_COLOR_SURFACE1, 0);
    lv_obj_set_style_border_width(g.frame, 2, 0);

    g.head = put_label(g.root, &lv_font_montserrat_bold_48, M(32), M(74), LV_TEXT_ALIGN_LEFT, 650);
    put_rich(&g.sub, g.root, M(32), M(108), LV_TEXT_ALIGN_LEFT, 650);

    for (int i = 0; i < 3; i++) {
        int32_t x = 698 + i * 182;
        g.box[i] = rect(g.root, M(x), M(24), 170, 80, 6);
        lv_obj_set_style_bg_color(g.box[i], WS_COLOR_BG, 0);
        lv_obj_set_style_border_color(g.box[i], WS_COLOR_SURFACE0, 0);
        lv_obj_set_style_border_width(g.box[i], 1, 0);
        /* Box children are placed in the box's own space, less its border. */
        lv_obj_t *l = put_label(g.box[i], &lv_font_montserrat_20, 13, 28 - 1, LV_TEXT_ALIGN_LEFT,
                                150);
        lv_obj_set_style_text_letter_space(l, 1, 0);
        set_color(l, WS_COLOR_MUTED);
        lv_label_set_text_static(l, BOX_TEXT[i]);
        g.box_val[i] = put_label(g.box[i], &lv_font_montserrat_bold_28, 13, 64 - 1,
                                 LV_TEXT_ALIGN_LEFT, 150);
    }

    g.pool_lbl = put_label(g.root, &lv_font_montserrat_bold_20, M(32), M(152), LV_TEXT_ALIGN_LEFT,
                           380);
    put_rich(&g.pool_txt, g.root, M(812), M(152), LV_TEXT_ALIGN_RIGHT, 400);
    bar_create(&g.pool_bar, g.root, M(32), M(164), 780, 28);
    g.root_lbl = put_label(g.root, &lv_font_montserrat_bold_20, M(836), M(152), LV_TEXT_ALIGN_LEFT,
                           150);
    lv_label_set_text_static(g.root_lbl, "root");
    put_rich(&g.root_txt, g.root, M(1232), M(152), LV_TEXT_ALIGN_RIGHT, 240);
    bar_create(&g.root_bar, g.root, M(836), M(164), 396, 28);

    g.rail = rect(g.root, M(32), M(222), 1200, 6, 3);

    for (int i = 0; i < TILES; i++)
        tile_create(&g.tiles[i], i);
    return g.root;
}

/* ---- Refresh ---- */

static lv_color_t level_color(nas_level_t l, lv_color_t ok) {
    switch (l) {
    case NAS_CRIT: return WS_COLOR_RED;
    case NAS_WARN: return WS_COLOR_YELLOW;
    default: return ok;
    }
}

/* The one extra fact a tile has room for, worst first. */
static void tile_extra(const nas_disk_t *d, char *buf, size_t n) {
    buf[0] = '\0';
    if (d->smart_passed == 0) return; /* the pill says FAILED */
    if (d->pending_sectors > 0) snprintf(buf, n, "pending %lld", (long long)d->pending_sectors);
    else if (d->media_errors > 0) snprintf(buf, n, "media err %lld", (long long)d->media_errors);
    else if (d->percentage_used >= 80) snprintf(buf, n, "wear %lld%%", (long long)d->percentage_used);
    else if (d->reallocated_sectors > 0)
        snprintf(buf, n, "realloc %lld", (long long)d->reallocated_sectors);
    else if (d->percentage_used >= 0) snprintf(buf, n, "wear %lld%%", (long long)d->percentage_used);
}

static void tile_update(tile_t *t, const nas_disk_t *d, bool stale, lv_color_t acc) {
    char buf[32];
    const nas_level_t lvl = nas_disk_level(d);
    const nas_use_t use = nas_disk_use(d);
    const bool standby = strcmp(d->power_state, "standby") == 0;
    const bool attn = !stale && lvl >= NAS_WARN;
    const lv_color_t txt = stale ? WS_COLOR_MUTED : WS_COLOR_FG;
    const lv_color_t subc = stale ? WS_COLOR_MUTED : C_SUBTEXT;
    lv_color_t c = acc;
    if (stale || (standby && !attn))
        c = WS_COLOR_MUTED;
    else if (attn)
        c = level_color(lvl, acc);

    set_bg(t->tick, acc);
    set_bg(t->tile, use == NAS_USE_COLD ? C_COLD : WS_COLOR_BG);
    set_bg(t->edge, c);

    set_text(t->use, nas_use_label(use));
    set_color(t->use, stale ? WS_COLOR_MUTED : use == NAS_USE_POOL ? WS_COLOR_ORANGE : C_SUBTEXT);
    set_text(t->dev, nas_short_device(d->device));
    set_color(t->dev, txt);
    set_text(t->kind, d->kind);

    if (d->has_temp)
        snprintf(buf, sizeof(buf), "%.0f\xc2\xb0", d->temp_c);
    else
        snprintf(buf, sizeof(buf), "\xe2\x80\x94");
    set_text(t->temp, buf);
    set_color(t->temp, stale ? WS_COLOR_MUTED : standby ? C_SUBTEXT : WS_COLOR_FG);
    bar_set(&t->tbar, d->has_temp ? (d->temp_c - 20.0) / 40.0 * 100.0 : 0, c);

    const bool hdd = strcmp(d->kind, "hdd") == 0;
    set_text(t->power, standby                                   ? "standby"
                       : strcmp(d->power_state, "active") != 0 ? "state ?"
                       : hdd                                     ? "spinning"
                                                                 : "active");
    set_color(t->power, subc);

    tile_extra(d, buf, sizeof(buf));
    set_text(t->extra, buf);
    set_font(t->extra, attn ? &lv_font_montserrat_bold_20 : &lv_font_montserrat_20);
    set_color(t->extra, attn ? level_color(lvl, subc) : subc);

    const char *pill = d->smart_passed == 1 ? "SMART" : d->smart_passed == 0 ? "FAILED" : "SMART ?";
    set_text(t->pill_lbl, pill);
    lv_obj_center(t->pill_lbl);
    lv_color_t pill_c = WS_COLOR_SURFACE1;
    if (!stale && d->smart_passed == 1)
        pill_c = acc;
    else if (!stale && d->smart_passed == 0)
        pill_c = WS_COLOR_RED;
    set_bg(t->pill, pill_c);
    set_color(t->pill_lbl, stale ? WS_COLOR_MUTED : d->smart_passed == -1 ? WS_COLOR_FG : C_CRUST);
}

void nas_card_update(lv_obj_t *card, double now) {
    if (!card || card != g.root)
        return;
    const nas_record_t *r = nas_record();
    const nas_feed_t feed = nas_feed(now);
    const bool stale = feed != NAS_FEED_LIVE;
    const lv_color_t acc = stale ? WS_COLOR_MUTED : WS_COLOR_MAUVE;
    const lv_color_t txt = stale ? WS_COLOR_MUTED : WS_COLOR_FG;
    const lv_color_t subc = stale ? WS_COLOR_MUTED : C_SUBTEXT;
    char head[64], sub[160], buf[64], age[16];
    nas_level_t lvl = NAS_OK;

    /* Headline and frame: the state, said once. */
    nas_headline(r, feed, now, head, sizeof(head), sub, sizeof(sub), &lvl);
    int order[TILES];
    int ntiles = nas_disk_order(r, order, TILES);
    if (!stale && r->ndisks > TILES) {
        size_t used = strlen(sub);
        snprintf(sub + used, sizeof(sub) - used, " \xc2\xb7 +%d not shown", r->ndisks - TILES);
    }
    set_text(g.head, head);
    set_color(g.head, feed == NAS_FEED_NONE ? WS_COLOR_MUTED
                      : stale               ? WS_COLOR_RED
                                            : level_color(lvl, WS_COLOR_FG));
    rich_set(&g.sub, sub, subc);
    const bool attn = !stale && lvl >= NAS_WARN;
    lv_color_t frame_c = stale ? WS_COLOR_MUTED : level_color(lvl, WS_COLOR_SURFACE1);
    if (!lv_color_eq(lv_obj_get_style_border_color(g.frame, 0), frame_c))
        lv_obj_set_style_border_color(g.frame, frame_c, 0);
    int32_t frame_w = (attn || stale) ? 3 : 2;
    if (lv_obj_get_style_border_width(g.frame, 0) != frame_w)
        lv_obj_set_style_border_width(g.frame, frame_w, 0);

    /* The first pool. gratch has one; a second would still count toward the
     * headline and the frame, it just gets no bar of its own. */
    const nas_pool_t *p = r->npools > 0 ? &r->pools[0] : NULL;
    double pool_pct = -1;
    if (p) {
        const bool online = strcmp(p->health, "ONLINE") == 0;
        snprintf(buf, sizeof(buf), "%s \xc2\xb7 %s", p->name, p->health);
        set_text(g.pool_lbl, buf);
        set_color(g.pool_lbl, online || stale ? txt : WS_COLOR_RED);
        int64_t total = (p->used_bytes >= 0 && p->avail_bytes >= 0) ? p->used_bytes + p->avail_bytes
                                                                     : -1;
        char used_s[16], total_s[16];
        nas_format_bytes(p->used_bytes, used_s, sizeof(used_s));
        nas_format_bytes(total, total_s, sizeof(total_s));
        pool_pct = total > 0 ? 100.0 * (double)p->used_bytes / (double)total : 0;
        snprintf(buf, sizeof(buf), "%s / %s \xc2\xb7 %.0f%%", used_s, total_s, pool_pct);
        rich_set(&g.pool_txt, buf, subc);
        bar_set(&g.pool_bar, pool_pct, online || stale ? acc : WS_COLOR_RED);
    } else {
        set_text(g.pool_lbl, "no pool");
        set_color(g.pool_lbl, txt);
        rich_set(&g.pool_txt, "", subc);
        bar_set(&g.pool_bar, 0, acc);
    }

    /* Root filesystem, from the host's own telemetry (not in kdash:nas). */
    const client_info_t *ci = registry_find(KPIDASH_NAS_HOST);
    const disk_entry_t *rootfs = NULL;
    for (int i = 0; ci && i < ci->disk_count; i++)
        if (strcmp(ci->disks[i].label, "root") == 0)
            rootfs = &ci->disks[i];
    set_color(g.root_lbl, txt);
    if (rootfs) {
        snprintf(buf, sizeof(buf), "%.0f / %.0f GB \xc2\xb7 %.0f%%", (double)rootfs->used_gb,
                 (double)rootfs->total_gb, (double)rootfs->pct);
        rich_set(&g.root_txt, buf, subc);
        bar_set(&g.root_bar, rootfs->pct, acc);
    } else {
        rich_set(&g.root_txt, "\xe2\x80\x94", subc);
        bar_set(&g.root_bar, 0, acc);
    }

    /* Stat boxes: pool %, hottest disk, data age. */
    if (pool_pct >= 0)
        snprintf(buf, sizeof(buf), "%.0f%%", pool_pct);
    else
        snprintf(buf, sizeof(buf), "\xe2\x80\x94");
    set_text(g.box_val[0], buf);
    double hottest = -1000;
    for (int i = 0; i < r->ndisks; i++)
        if (r->disks[i].has_temp && r->disks[i].temp_c > hottest)
            hottest = r->disks[i].temp_c;
    if (hottest > -1000)
        snprintf(buf, sizeof(buf), "%.0f\xc2\xb0", hottest);
    else
        snprintf(buf, sizeof(buf), "\xe2\x80\x94");
    set_text(g.box_val[1], buf);
    if (r->has_collected) {
        nas_format_age(now - r->collected_ts, age, sizeof(age));
        set_text(g.box_val[2], age);
    } else {
        set_text(g.box_val[2], "\xe2\x80\x94");
    }
    for (int i = 0; i < 3; i++)
        set_color(g.box_val[i], txt);

    /* The bus and one tile per disk, in Ken's order. */
    set_bg(g.rail, acc);
    for (int i = 0; i < TILES; i++) {
        tile_t *t = &g.tiles[i];
        bool show = i < ntiles;
        set_hidden(t->tick, !show);
        set_hidden(t->tile, !show);
        if (show)
            tile_update(t, &r->disks[order[i]], stale, acc);
    }
}
