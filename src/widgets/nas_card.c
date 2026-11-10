/* nas_card.c — the NAS big panel (WI #3956). See nas_card.h.
 *
 * Every text label uses a BOLD Montserrat: those are generated with Latin-1
 * (fonts/generate.sh), so "°" and "·" render; the regular sizes are ASCII
 * only.
 *
 * Staleness is explicit, as on the host cards: a stale feed turns the border
 * grey and the header red with its age, and keeps the last values in view
 * rather than blanking them. A standby disk is neutral grey, never a fault. */
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
#define BORDER 3
#define LEFT_X 20
#define LEFT_W 590
#define RIGHT_X 640
#define RIGHT_W (NAS_W - RIGHT_X - 20)
#define TOP_Y 64
#define BAR_H 38
#define BAR_BLOCK 82 /* label line + bar + gap */
#define ROW_H 62
#define ROWS_MAX 8   /* (NAS_H - TOP_Y) / ROW_H */
#define CHART_POINTS 120

#define C_MANTLE lv_color_hex(0x181825)
#define C_SUBTEXT lv_color_hex(0xA6ADC8)
#define C_BAR_BG lv_color_hex(0x313244)

typedef struct {
    lv_obj_t *label;
    lv_obj_t *detail;
    lv_obj_t *bar;
    lv_obj_t *bar_text;
} bar_row_t;

typedef struct {
    lv_obj_t *stripe;
    lv_obj_t *name;
    lv_obj_t *detail;
} disk_row_t;

static struct {
    lv_obj_t *cont;
    lv_obj_t *title;
    lv_obj_t *status;
    bar_row_t bars[NAS_POOLS_MAX + 1]; /* pools, then root */
    lv_obj_t *chart;
    lv_obj_t *chart_label;
    lv_chart_series_t *cpu_ser;
    lv_chart_series_t *ram_ser;
    double chart_last_ts;
    disk_row_t rows[ROWS_MAX];
    lv_obj_t *more;
} g;

static lv_color_t level_color(nas_level_t l) {
    switch (l) {
    case NAS_CRIT: return WS_COLOR_RED;
    case NAS_WARN: return WS_COLOR_YELLOW;
    case NAS_NEUTRAL: return WS_COLOR_MUTED;
    case NAS_OK:
    default: return WS_COLOR_GREEN;
    }
}

static lv_obj_t *label(lv_obj_t *parent, const lv_font_t *f, lv_color_t c) {
    lv_obj_t *l = lv_label_create(parent);
    lv_obj_set_style_text_font(l, f, 0);
    lv_obj_set_style_text_color(l, c, 0);
    lv_label_set_text(l, "");
    return l;
}

static void build_bar(bar_row_t *b) {
    b->label = label(g.cont, &lv_font_montserrat_bold_20, WS_COLOR_FG);
    b->detail = label(g.cont, &lv_font_montserrat_bold_16, C_SUBTEXT);
    lv_obj_set_width(b->detail, LEFT_W / 2);
    lv_obj_set_style_text_align(b->detail, LV_TEXT_ALIGN_RIGHT, 0);
    lv_label_set_long_mode(b->detail, LV_LABEL_LONG_CLIP);
    b->bar = lv_bar_create(g.cont);
    lv_obj_set_size(b->bar, LEFT_W, BAR_H);
    lv_obj_set_style_bg_color(b->bar, C_BAR_BG, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(b->bar, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_radius(b->bar, 4, LV_PART_MAIN);
    lv_obj_set_style_radius(b->bar, 4, LV_PART_INDICATOR);
    lv_bar_set_range(b->bar, 0, 100);
    b->bar_text = label(b->bar, &lv_font_montserrat_bold_16, WS_COLOR_FG);
    lv_obj_align(b->bar_text, LV_ALIGN_LEFT_MID, 10, 0);
}

static void place_bar(bar_row_t *b, int y, bool show) {
    lv_obj_t *objs[] = {b->label, b->detail, b->bar};
    for (size_t i = 0; i < sizeof(objs) / sizeof(objs[0]); i++) {
        if (show) lv_obj_clear_flag(objs[i], LV_OBJ_FLAG_HIDDEN);
        else lv_obj_add_flag(objs[i], LV_OBJ_FLAG_HIDDEN);
    }
    if (!show) return;
    lv_obj_set_pos(b->label, LEFT_X, y);
    lv_obj_set_pos(b->detail, LEFT_X + LEFT_W / 2, y + 3);
    lv_obj_set_pos(b->bar, LEFT_X, y + 30);
}

static void set_bar(bar_row_t *b, double used, double total, lv_color_t ind) {
    int pct = total > 0 ? (int)(used * 100.0 / total + 0.5) : 0;
    if (pct < 0) pct = 0;
    if (pct > 100) pct = 100;
    lv_bar_set_value(b->bar, pct, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(b->bar, ind, LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(b->bar, LV_OPA_COVER, LV_PART_INDICATOR);
}

lv_obj_t *nas_card_create(lv_obj_t *parent) {
    memset(&g, 0, sizeof(g));
    g.cont = lv_obj_create(parent);
    lv_obj_clear_flag(g.cont, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(g.cont, NAS_W, NAS_H);
    lv_obj_set_style_bg_color(g.cont, C_MANTLE, 0);
    lv_obj_set_style_bg_opa(g.cont, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(g.cont, 8, 0);
    lv_obj_set_style_border_width(g.cont, BORDER, 0);
    lv_obj_set_style_border_color(g.cont, WS_COLOR_MUTED, 0);
    lv_obj_set_style_pad_all(g.cont, 0, 0);

    g.title = label(g.cont, &lv_font_montserrat_bold_28, WS_COLOR_ORANGE);
    lv_label_set_text(g.title, KPIDASH_NAS_HOST " \xc2\xb7 NAS");
    lv_obj_set_pos(g.title, LEFT_X, 14);

    g.status = label(g.cont, &lv_font_montserrat_bold_20, C_SUBTEXT);
    lv_obj_set_width(g.status, 700);
    lv_obj_set_style_text_align(g.status, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_set_pos(g.status, NAS_W - 20 - 700, 20);

    for (int i = 0; i < NAS_POOLS_MAX + 1; i++) build_bar(&g.bars[i]);

    g.chart_label = label(g.cont, &lv_font_montserrat_bold_16, C_SUBTEXT);
    g.chart = lv_chart_create(g.cont);
    lv_obj_set_style_bg_color(g.chart, WS_COLOR_BG, 0);
    lv_obj_set_style_bg_opa(g.chart, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(g.chart, 0, 0);
    lv_obj_set_style_line_color(g.chart, WS_COLOR_SURFACE0, LV_PART_MAIN);
    lv_obj_set_style_line_width(g.chart, 1, LV_PART_MAIN);
    lv_obj_set_style_line_width(g.chart, 2, LV_PART_ITEMS);
    lv_obj_set_style_size(g.chart, 0, 0, LV_PART_INDICATOR);
    lv_obj_clear_flag(g.chart, LV_OBJ_FLAG_SCROLLABLE);
    lv_chart_set_type(g.chart, LV_CHART_TYPE_LINE);
    lv_chart_set_update_mode(g.chart, LV_CHART_UPDATE_MODE_SHIFT);
    lv_chart_set_point_count(g.chart, CHART_POINTS);
    lv_chart_set_div_line_count(g.chart, 3, 0);
    lv_chart_set_range(g.chart, LV_CHART_AXIS_PRIMARY_Y, 0, 100);
    g.cpu_ser = lv_chart_add_series(g.chart, WS_COLOR_BLUE, LV_CHART_AXIS_PRIMARY_Y);
    g.ram_ser = lv_chart_add_series(g.chart, WS_COLOR_RED, LV_CHART_AXIS_PRIMARY_Y);
    lv_chart_set_all_value(g.chart, g.cpu_ser, LV_CHART_POINT_NONE);
    lv_chart_set_all_value(g.chart, g.ram_ser, LV_CHART_POINT_NONE);

    for (int i = 0; i < ROWS_MAX; i++) {
        disk_row_t *r = &g.rows[i];
        int y = TOP_Y + i * ROW_H;
        r->stripe = lv_obj_create(g.cont);
        lv_obj_clear_flag(r->stripe, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_size(r->stripe, 8, ROW_H - 12);
        lv_obj_set_style_radius(r->stripe, 3, 0);
        lv_obj_set_style_border_width(r->stripe, 0, 0);
        lv_obj_set_pos(r->stripe, RIGHT_X, y + 4);
        r->name = label(g.cont, &lv_font_montserrat_bold_20, WS_COLOR_FG);
        lv_obj_set_width(r->name, RIGHT_W - 20);
        lv_label_set_long_mode(r->name, LV_LABEL_LONG_CLIP);
        lv_obj_set_pos(r->name, RIGHT_X + 20, y);
        r->detail = label(g.cont, &lv_font_montserrat_bold_16, C_SUBTEXT);
        lv_obj_set_width(r->detail, RIGHT_W - 20);
        lv_label_set_long_mode(r->detail, LV_LABEL_LONG_CLIP);
        lv_obj_set_pos(r->detail, RIGHT_X + 20, y + 28);
    }
    g.more = label(g.cont, &lv_font_montserrat_bold_16, C_SUBTEXT);
    lv_obj_set_pos(g.more, RIGHT_X + 20, NAS_H - 26);
    return g.cont;
}

static const char *short_device(const char *dev) {
    const char *s = strrchr(dev, '/');
    return s ? s + 1 : dev;
}

static void update_disk_row(disk_row_t *r, const nas_disk_t *d, bool stale) {
    char name[96], detail[160];
    snprintf(name, sizeof(name), "%s  %s", short_device(d->device), d->model);
    lv_label_set_text(r->name, name);

    nas_level_t lvl = nas_disk_level(d);
    size_t used = 0;
    detail[0] = '\0';
#define ADD(...)                                                                                   \
    do {                                                                                           \
        int w_ = snprintf(detail + used, sizeof(detail) - used, __VA_ARGS__);                      \
        if (w_ > 0 && (size_t)w_ < sizeof(detail) - used) used += (size_t)w_;                      \
    } while (0)
    ADD("%s", d->kind);
    if (d->has_temp) ADD(" \xc2\xb7 %.0f\xc2\xb0" "C", d->temp_c);
    ADD(" \xc2\xb7 %s", strcmp(d->power_state, "standby") == 0 ? "standby" :
                        strcmp(d->power_state, "active") == 0  ? "spinning" : "state ?");
    ADD(" \xc2\xb7 SMART %s",
        d->smart_passed == 1 ? "ok" : d->smart_passed == 0 ? "FAILED" : "?");
    if (d->percentage_used >= 0) ADD(" \xc2\xb7 wear %lld%%", (long long)d->percentage_used);
    if (d->reallocated_sectors > 0)
        ADD(" \xc2\xb7 realloc %lld", (long long)d->reallocated_sectors);
    if (d->pending_sectors > 0) ADD(" \xc2\xb7 pending %lld", (long long)d->pending_sectors);
    if (d->media_errors > 0) ADD(" \xc2\xb7 media err %lld", (long long)d->media_errors);
#undef ADD
    lv_label_set_text(r->detail, detail);

    lv_color_t c = stale ? WS_COLOR_MUTED : level_color(lvl);
    lv_obj_set_style_bg_color(r->stripe, c, 0);
    lv_obj_set_style_text_color(r->detail,
                                (!stale && (lvl == NAS_WARN || lvl == NAS_CRIT)) ? c : C_SUBTEXT,
                                0);
}

static void update_chart(double now) {
    graph_host_series_t *s = graph_host_find_or_create(KPIDASH_NAS_HOST);
    if (!s || !s->telemetry.valid || s->last_sample_ts <= g.chart_last_ts) return;
    g.chart_last_ts = s->last_sample_ts;
    (void)now;
    int ram = s->telemetry.ram_total_mb
                  ? (int)(100.0 * s->telemetry.ram_used_mb / s->telemetry.ram_total_mb)
                  : 0;
    lv_chart_set_next_value(g.chart, g.cpu_ser, (int32_t)s->telemetry.cpu_pct);
    lv_chart_set_next_value(g.chart, g.ram_ser, ram);
    lv_chart_refresh(g.chart);
    char buf[96];
    snprintf(buf, sizeof(buf), "activity \xc2\xb7 CPU %.0f%% \xc2\xb7 RAM %d%%",
             (double)s->telemetry.cpu_pct, ram);
    lv_label_set_text(g.chart_label, buf);
}

void nas_card_update(lv_obj_t *card, double now) {
    if (!card || card != g.cont) return;
    const nas_record_t *r = nas_record();
    nas_feed_t feed = nas_feed(now);
    bool stale = feed != NAS_FEED_LIVE;
    nas_level_t overall = nas_overall_level(r);

    /* Header: freshness first, then the collector. */
    char buf[160], age[16];
    lv_color_t status_c = C_SUBTEXT;
    if (feed == NAS_FEED_NONE) {
        snprintf(buf, sizeof(buf), "NO FEED");
        status_c = WS_COLOR_MUTED;
    } else if (stale) {
        nas_format_age(now - r->ts, age, sizeof(age));
        snprintf(buf, sizeof(buf), "STALE \xc2\xb7 %s", age);
        status_c = WS_COLOR_RED;
    } else if (!r->has_collected) {
        snprintf(buf, sizeof(buf), "collector down");
        status_c = WS_COLOR_YELLOW;
    } else {
        nas_format_age(now - r->collected_ts, age, sizeof(age));
        snprintf(buf, sizeof(buf), "%scollected %s ago", r->source_stale ? "STALE SOURCE \xc2\xb7 " : "",
                 age);
        if (r->source_stale) status_c = WS_COLOR_YELLOW;
    }
    lv_label_set_text(g.status, buf);
    lv_obj_set_style_text_color(g.status, status_c, 0);
    lv_obj_set_style_border_color(g.cont, stale ? WS_COLOR_MUTED : level_color(overall), 0);

    /* Left: pools, then root. */
    int y = TOP_Y;
    int nbar = 0;
    for (int i = 0; i < r->npools && i < NAS_POOLS_MAX; i++, nbar++) {
        const nas_pool_t *p = &r->pools[i];
        bar_row_t *b = &g.bars[nbar];
        nas_level_t pl = nas_pool_level(p);
        place_bar(b, y, true);
        snprintf(buf, sizeof(buf), "%s \xc2\xb7 %s", p->name, p->health);
        lv_label_set_text(b->label, buf);
        lv_obj_set_style_text_color(b->label, (!stale && pl != NAS_OK) ? level_color(pl)
                                                                        : WS_COLOR_FG, 0);
        if (p->has_scrub && strcmp(p->scrub_state, "scanning") == 0) {
            snprintf(buf, sizeof(buf), "scrubbing");
        } else if (p->has_scrub && p->scrub_end_ts > 0) {
            nas_format_age(now - p->scrub_end_ts, age, sizeof(age));
            snprintf(buf, sizeof(buf), "scrub %s ago \xc2\xb7 %lld err", age,
                     (long long)(p->scrub_errors > 0 ? p->scrub_errors : 0));
        } else {
            snprintf(buf, sizeof(buf), "no scrub");
        }
        lv_label_set_text(b->detail, buf);
        char used_s[16], total_s[16];
        nas_format_bytes(p->used_bytes, used_s, sizeof(used_s));
        int64_t total = (p->used_bytes >= 0 && p->avail_bytes >= 0) ? p->used_bytes + p->avail_bytes
                                                                     : -1;
        nas_format_bytes(total, total_s, sizeof(total_s));
        double pct = total > 0 ? 100.0 * (double)p->used_bytes / (double)total : 0;
        snprintf(buf, sizeof(buf), "%s / %s \xc2\xb7 %.0f%%", used_s, total_s, pct);
        lv_label_set_text(b->bar_text, buf);
        set_bar(b, (double)p->used_bytes, (double)total,
                stale ? WS_COLOR_MUTED : pct > 85 ? WS_COLOR_RED : pct > 70 ? WS_COLOR_YELLOW
                                                                            : WS_COLOR_TEAL);
        y += BAR_BLOCK;
    }

    /* Root filesystem, from the host's own telemetry (not in kdash:nas). */
    const client_info_t *ci = registry_find(KPIDASH_NAS_HOST);
    const disk_entry_t *root = NULL;
    for (int i = 0; ci && i < ci->disk_count; i++)
        if (strcmp(ci->disks[i].label, "root") == 0) root = &ci->disks[i];
    if (root && nbar < NAS_POOLS_MAX + 1) {
        bar_row_t *b = &g.bars[nbar++];
        place_bar(b, y, true);
        lv_label_set_text(b->label, "root");
        lv_obj_set_style_text_color(b->label, WS_COLOR_FG, 0);
        lv_label_set_text(b->detail, "boot disk");
        snprintf(buf, sizeof(buf), "%.0f GB / %.0f GB \xc2\xb7 %.0f%%", (double)root->used_gb,
                 (double)root->total_gb, (double)root->pct);
        lv_label_set_text(b->bar_text, buf);
        set_bar(b, root->used_gb, root->total_gb,
                root->pct > 85 ? WS_COLOR_RED : root->pct > 70 ? WS_COLOR_YELLOW : WS_COLOR_TEAL);
        y += BAR_BLOCK;
    }
    for (int i = nbar; i < NAS_POOLS_MAX + 1; i++) place_bar(&g.bars[i], 0, false);

    /* Activity chart fills what is left of the left column. */
    lv_obj_set_pos(g.chart_label, LEFT_X, y);
    int chart_y = y + 26;
    int chart_h = NAS_H - 16 - chart_y;
    if (chart_h < 60) chart_h = 60;
    lv_obj_set_pos(g.chart, LEFT_X, chart_y);
    lv_obj_set_size(g.chart, LEFT_W, chart_h);
    update_chart(now);

    /* Right: one row per disk. */
    int nrows = r->ndisks < ROWS_MAX ? r->ndisks : ROWS_MAX;
    for (int i = 0; i < ROWS_MAX; i++) {
        disk_row_t *dr = &g.rows[i];
        bool show = i < nrows;
        lv_obj_t *objs[] = {dr->stripe, dr->name, dr->detail};
        for (size_t k = 0; k < 3; k++) {
            if (show) lv_obj_clear_flag(objs[k], LV_OBJ_FLAG_HIDDEN);
            else lv_obj_add_flag(objs[k], LV_OBJ_FLAG_HIDDEN);
        }
        if (show) update_disk_row(dr, &r->disks[i], stale);
    }
    if (r->ndisks > ROWS_MAX) {
        snprintf(buf, sizeof(buf), "+%d more", r->ndisks - ROWS_MAX);
        lv_label_set_text(g.more, buf);
    } else {
        lv_label_set_text(g.more, "");
    }
}
