/**
 * agstate.c — the Agents card's model (WI #4695, v2 #4733). See the header.
 */
#include "agstate.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <kdash/kdash_freshness.h>

/* ---- the record ---- */

/* The one instance, and a scratch to parse into so a rejected record never
 * touches it. Both static: 21,544 bytes each, never on the stack. */
static kdash_agents_t g_rec, g_scratch;
static bool g_have;
static unsigned g_gen;
static char *g_raw; /* the last value seen, so an unchanged poll is a strcmp */

int ag_observe(const char *json) {
    if (!json)
        return 0;
    if (g_raw && strcmp(json, g_raw) == 0)
        return 0;
    free(g_raw);
    g_raw = strdup(json);
    if (!kdash_parse_agents(json, strlen(json), &g_scratch))
        return -1;
    if (g_have && memcmp(&g_scratch, &g_rec, sizeof(g_rec)) == 0)
        return 0;
    g_rec = g_scratch;
    g_have = true;
    g_gen++;
    return 1;
}

const kdash_agents_t *ag_record(void) {
    return g_have ? &g_rec : NULL;
}

unsigned ag_generation(void) {
    return g_gen;
}

void ag_reset(void) {
    free(g_raw);
    g_raw = NULL;
    memset(&g_rec, 0, sizeof(g_rec));
    g_have = false;
    g_gen++;
}

uint8_t ag_host_slot(const char *host) {
    if (!host)
        return AG_HOST_OTHER;
    if (strcmp(host, "cleo") == 0)
        return AG_HOST_CLEO;
    if (strcmp(host, "kai") == 0)
        return AG_HOST_KAI;
    if (strcmp(host, "kubs0") == 0)
        return AG_HOST_KUBS0;
    return AG_HOST_OTHER;
}

/* ---- the label ---- */

bool ag_fit_label(const char *name, int32_t avail, ag_measure_fn measure, void *ctx, char *out,
                  size_t n) {
    if (!n)
        return false;
    out[0] = '\0';
    if (!name || !name[0] || avail <= 0)
        return false;
    snprintf(out, n, "%s", name);
    if (measure(out, ctx) <= avail)
        return true;
    /* Character boundaries of the name, then the longest prefix that fits
     * with "…" after it. */
    size_t len = strlen(out);
    size_t cut[KDASH_AGENTS_NAME_MAX];
    int nch = 0;
    for (size_t i = 0; i < len && nch < (int)(sizeof(cut) / sizeof(cut[0])); i++)
        if (((unsigned char)out[i] & 0xC0) != 0x80)
            cut[nch++] = i;
    char buf[KDASH_AGENTS_NAME_MAX + 4];
    for (int k = nch - 1; k >= AG_LABEL_MIN_CHARS; k--) {
        size_t end = cut[k];
        while (end > 0 && out[end - 1] == ' ')
            end--; /* no space before the ellipsis */
        if (end + 4 > sizeof(buf))
            continue;
        memcpy(buf, out, end);
        memcpy(buf + end, "\xE2\x80\xA6", 4); /* … and the NUL */
        if (measure(buf, ctx) <= avail) {
            snprintf(out, n, "%s", buf);
            return true;
        }
    }
    out[0] = '\0';
    return false;
}

/* ---- the view ---- */

/* The old-ts hint, coarsest unit: "as of 40m", "as of 3h", "as of 2d". The
 * modulo bounds the number so -O3's -Wformat-truncation can prove it fits. */
static void fmt_hint(double secs, char *buf, size_t n) {
    unsigned long s = secs > 0 ? (unsigned long)secs : 0;
    unsigned long val = s < 3600 ? s / 60 : s < 2 * 86400 ? s / 3600 : s / 86400;
    char unit = s < 3600 ? 'm' : s < 2 * 86400 ? 'h' : 'd';
    snprintf(buf, n, "as of %u%c", (unsigned)(val % 1000u), unit);
}

/* Frame x of time `t` on an axis `span` seconds long ending at ws + span. */
static int32_t xof(double t, double ws, double span) {
    double f = (t - ws) / span;
    if (f < 0)
        f = 0;
    if (f > 1)
        f = 1;
    return AG_PLOT_X0 + (int32_t)lround(f * (AG_PLOT_X1 - AG_PLOT_X0));
}

/* A bar the window still shows, before packing. */
typedef struct {
    int src;      /* index in the record */
    int parent;   /* index in the candidates, or -1 */
    double end;   /* where its right edge is, in time: drops oldest first */
    int32_t x0, x1;
    int32_t left; /* x0, or the left edge of a name written outside */
    int row;
    bool keep;
    bool running;
    bool label_out;
    char label[KDASH_AGENTS_NAME_MAX + 4];
} cand_t;

/* Pack the kept candidates into rows. Every root (a bar with no parent) in
 * start order goes in the highest row free at its left edge (the name's,
 * when the name is outside); an overseer's
 * legs follow it at once, each in the highest free row BELOW it (and a leg's
 * own legs below that). A row is free when its last bar ended AG_PACK_GAP_PX
 * before this one starts; each bar placed in a row starts after the last one
 * there ended, so the last is also the rightmost. Returns the rows used. */
typedef struct {
    int32_t rowend[KDASH_AGENTS_BARS_MAX];
    int rows;
} packer_t;

static void place(cand_t *c, int n, int i, int minrow, packer_t *p) {
    int t = minrow;
    while (t < p->rows && p->rowend[t] > c[i].left)
        t++;
    if (t == p->rows)
        p->rowend[p->rows++] = 0;
    c[i].row = t;
    p->rowend[t] = c[i].x1 + AG_PACK_GAP_PX;
    for (int j = 0; j < n; j++)
        if (c[j].keep && c[j].row < 0 && c[j].parent == i)
            place(c, n, j, t + 1, p);
}

static int pack(cand_t *c, int n) {
    packer_t p = {.rows = 0};
    for (int i = 0; i < n; i++)
        c[i].row = -1;
    for (int i = 0; i < n; i++)
        if (c[i].keep && c[i].parent < 0)
            place(c, n, i, 0, &p);
    /* A parent chain with no root (two overseers naming each other): place
     * what is left as roots, so nothing is silently undrawn. */
    for (int i = 0; i < n; i++)
        if (c[i].keep && c[i].row < 0)
            place(c, n, i, 0, &p);
    return p.rows;
}

/* The strip's grid: every even local hour inside (ws, now). */
static void hours(time_t ws, time_t now, ag_view_t *v) {
    time_t t = ws - (ws % 3600) + (ws % 3600 ? 3600 : 0);
    for (; t < now && v->nhours < AG_HOURS_MAX; t += 3600) {
        if (t <= ws)
            continue;
        struct tm tm;
        if (!localtime_r(&t, &tm) || tm.tm_min != 0 || tm.tm_hour % 2)
            continue;
        ag_hour_t *h = &v->hours[v->nhours++];
        h->x = xof((double)t, (double)ws, AG_WINDOW_S);
        h->hour = tm.tm_hour;
        h->midnight = tm.tm_hour == 0;
    }
}

/* The detail grid: every local half hour inside (ds, now]. */
static void ticks(time_t ds, time_t now, ag_view_t *v) {
    time_t t = ds - (ds % 60) + 60;
    for (; t <= now && v->nticks < AG_TICKS_MAX; t += 60) {
        struct tm tm;
        if (!localtime_r(&t, &tm) || tm.tm_min % 30)
            continue;
        ag_tick_t *k = &v->ticks[v->nticks++];
        k->x = xof((double)t, (double)ds, AG_DETAIL_S);
        k->hour = tm.tm_hour;
        k->min = tm.tm_min;
        k->on_hour = tm.tm_min == 0;
    }
}

/* Where a bar's name goes. Inside, by ag_fit_label's rule (cut with "…" if
 * at least AG_LABEL_MIN_CHARS survive). Otherwise whole, left of the bar,
 * unless it would cross the plot's left edge: then nowhere. */
static void place_label(cand_t *k, const kdash_agent_bar_t *b, ag_measure_fn measure, void *ctx) {
    k->left = k->x0;
    k->label_out = false;
    k->label[0] = '\0';
    if (!measure || !b->name[0])
        return;
    const int32_t avail =
        (k->x1 - k->x0) - AG_LABEL_PAD - (b->shipped ? AG_LABEL_SHIP : AG_LABEL_PAD_R);
    if (ag_fit_label(b->name, avail, measure, ctx, k->label, sizeof(k->label)))
        return;
    const int32_t lx = k->x0 - AG_LABEL_OUT_GAP - measure(b->name, ctx);
    if (lx < AG_PLOT_X0)
        return;
    snprintf(k->label, sizeof(k->label), "%s", b->name);
    k->label_out = true;
    k->left = lx;
}

/* The day strip: every bar still in the 24 hours, in its host's lane. */
static void strip(const kdash_agents_t *rec, time_t ws, time_t now, ag_view_t *v) {
    for (int i = 0; i < rec->bars_count && i < KDASH_AGENTS_BARS_MAX; i++) {
        const kdash_agent_bar_t *b = &rec->bars[i];
        double e = kdash_agents_bar_end(rec, b, (long long)now);
        double s = b->start < (double)now ? b->start : (double)now;
        if (e < (double)ws || e < s)
            continue;
        ag_strip_t *k = &v->strip[v->nstrip++];
        k->x0 = xof(s, (double)ws, AG_WINDOW_S);
        k->x1 = xof(e, (double)ws, AG_WINDOW_S);
        if (k->x1 - k->x0 < AG_STRIP_MIN_W) {
            k->x1 = k->x0 + AG_STRIP_MIN_W;
            if (k->x1 > AG_PLOT_X1) {
                k->x1 = AG_PLOT_X1;
                k->x0 = k->x1 - AG_STRIP_MIN_W;
            }
        }
        k->lane = ag_host_slot(b->host);
        k->waiting = b->state == KDASH_AGENT_WAITING;
    }
}

void ag_view(time_t now, ag_measure_fn measure, void *ctx, ag_view_t *v) {
    memset(v, 0, sizeof(*v));
    const time_t nowm = now - (now % 60 + 60) % 60;
    const time_t ws = nowm - AG_WINDOW_S, ds = nowm - AG_DETAIL_S;
    v->now = nowm;
    v->band_x0 = xof((double)ds, (double)ws, AG_WINDOW_S);
    hours(ws, nowm, v);
    ticks(ds, nowm, v);

    const kdash_agents_t *rec = ag_record();
    if (!rec) {
        v->status = AG_NOFEED;
        return;
    }
    v->stale = kdash_cadence_stale(rec->ts, rec->expected_refresh_s, (long long)nowm,
                                   KDASH_CADENCE_GRACE_S);
    if (v->stale)
        fmt_hint((double)nowm - rec->ts, v->hint, sizeof(v->hint));
    strip(rec, ws, nowm, v);

    /* The candidates: every bar whose right edge is still in the detail
     * window; one that started before it is clipped at the left edge. */
    static cand_t c[KDASH_AGENTS_BARS_MAX];
    int map[KDASH_AGENTS_BARS_MAX]; /* record index -> candidate, or -1 */
    int n = 0;
    for (int i = 0; i < rec->bars_count && i < KDASH_AGENTS_BARS_MAX; i++) {
        map[i] = -1;
        const kdash_agent_bar_t *b = &rec->bars[i];
        double e = kdash_agents_bar_end(rec, b, (long long)nowm);
        double s = b->start < (double)nowm ? b->start : (double)nowm;
        if (e < (double)ds || e < s)
            continue;
        cand_t *k = &c[n];
        k->src = i;
        k->end = e;
        k->running = b->state != KDASH_AGENT_DONE && e >= (double)nowm;
        k->x0 = xof(s, (double)ds, AG_DETAIL_S);
        k->x1 = xof(e, (double)ds, AG_DETAIL_S);
        if (k->x1 - k->x0 < AG_BAR_MIN_W) {
            k->x1 = k->x0 + AG_BAR_MIN_W;
            if (k->x1 > AG_PLOT_X1) {
                k->x1 = AG_PLOT_X1;
                k->x0 = k->x1 - AG_BAR_MIN_W;
            }
        }
        place_label(k, b, measure, ctx);
        k->keep = true;
        map[i] = n++;
    }
    for (int j = 0; j < n; j++) {
        int p = rec->bars[c[j].src].parent_index;
        c[j].parent = (p >= 0 && p < rec->bars_count) ? map[p] : -1;
    }

    /* Too many rows: leave out the bar that ended longest ago (an overseer
     * takes its legs with it) and pack again, until it fits. Rows never
     * shrink below AG_ROW_H_MIN; the newest work is what the card is for. */
    int rows;
    while ((rows = pack(c, n)) > AG_ROWS_MAX) {
        int old = -1;
        for (int j = 0; j < n; j++)
            if (c[j].keep && (old < 0 || c[j].end < c[old].end))
                old = j;
        c[old].keep = false;
        v->hidden++;
        if (rec->bars[c[old].src].overseer)
            for (int j = 0; j < n; j++)
                if (c[j].keep && c[j].parent == old) {
                    c[j].keep = false;
                    v->hidden++;
                }
    }

    v->rows = rows;
    if (rows > 0) {
        int32_t h = (AG_ROW_BOT - AG_ROW_TOP - (rows - 1) * AG_ROW_GAP) / rows;
        v->row_h = h > AG_ROW_H_MAX ? AG_ROW_H_MAX : h;
    }
    int out[KDASH_AGENTS_BARS_MAX]; /* candidate -> view index */
    for (int j = 0; j < n; j++) {
        out[j] = -1;
        if (!c[j].keep)
            continue;
        const kdash_agent_bar_t *b = &rec->bars[c[j].src];
        ag_bar_t *a = &v->bars[v->nbars];
        out[j] = v->nbars++;
        a->x0 = c[j].x0;
        a->x1 = c[j].x1;
        a->row = c[j].row;
        a->h = v->row_h;
        a->y = AG_ROW_TOP + c[j].row * (v->row_h + AG_ROW_GAP);
        a->host = ag_host_slot(b->host);
        a->state = (uint8_t)b->state;
        a->running = c[j].running;
        a->shipped = b->shipped;
        a->overseer = b->overseer;
        a->label_out = c[j].label_out;
        snprintf(a->name, sizeof(a->name), "%s", b->name);
        snprintf(a->label, sizeof(a->label), "%s", c[j].label);
    }
    for (int j = 0; j < n; j++)
        if (out[j] >= 0)
            v->bars[out[j]].parent = c[j].parent >= 0 ? out[c[j].parent] : -1;
    v->status = v->nbars ? AG_BARS : v->nstrip ? AG_QUIET : AG_EMPTY;
}
