/**
 * hostcols.c — Fill rule, host order and display model for the host columns
 * widget (WI #4524, #4525). Pure: see hostcols.h.
 */
#include "hostcols.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void hostcols_plan(int n, int *full, int *compact) {
    if (n < 0)
        n = 0;
    if (n > HOSTCOLS_MAX)
        n = HOSTCOLS_MAX;
    int f = n;
    if (n > HOSTCOLS_COLS) {
        /* Each host demoted to a tile frees a third of a column, i.e. two
         * full columns' worth of hosts fit in the space three tiles take. */
        f = (HOSTCOLS_MAX - n) / (HOSTCOLS_PER - 1);
        if (f < 0)
            f = 0;
    }
    if (full)
        *full = f;
    if (compact)
        *compact = n - f;
}

int hostcols_cmp(const client_info_t *a, const client_info_t *b) {
    int pa = registry_priority_index(a->hostname);
    int pb = registry_priority_index(b->hostname);
    if (pa >= 0 && pb >= 0)
        return pa - pb;
    if (pa >= 0)
        return -1;
    if (pb >= 0)
        return 1;
    return strncmp(a->hostname, b->hostname, HOSTNAME_LEN);
}

static int cmp_ptr(const void *x, const void *y) {
    return hostcols_cmp(*(const client_info_t *const *)x, *(const client_info_t *const *)y);
}

int hostcols_select(const client_info_t *in, int n, const client_info_t **out, int max,
                    int *dropped) {
    /* Sort every eligible host first, then cut: the cut has to fall on the
     * lowest priorities, not on whoever registered last. */
    const client_info_t *all[MAX_CLIENTS];
    int k = 0;
    for (int i = 0; i < n && k < MAX_CLIENTS; i++) {
        if (client_presence(&in[i]) == CLIENT_PRESENCE_ASLEEP)
            continue;
        all[k++] = &in[i];
    }
    qsort(all, (size_t)k, sizeof(all[0]), cmp_ptr);
    int m = k < max ? k : max;
    for (int i = 0; i < m; i++)
        out[i] = all[i];
    if (dropped)
        *dropped = k - m;
    return m;
}

hc_att_t hostcols_ram_att(int pct) {
    if (pct >= HOSTCOLS_RAM_CRIT)
        return HC_ATT_CRIT;
    if (pct >= HOSTCOLS_RAM_WARN)
        return HC_ATT_WARN;
    return HC_ATT_NONE;
}

hc_att_t hostcols_disk_att(int pct) {
    if (pct >= HOSTCOLS_DISK_CRIT)
        return HC_ATT_CRIT;
    if (pct >= HOSTCOLS_DISK_WARN)
        return HC_ATT_WARN;
    return HC_ATT_NONE;
}

static int round_pct(float v) {
    if (v < 0.0f)
        return 0;
    if (v > 100.0f)
        return 100;
    return (int)(v + 0.5f);
}

static void format_uptime(float seconds, char *buf, size_t len) {
    if (seconds <= 0.0f) {
        snprintf(buf, len, "up --");
        return;
    }
    long s = (long)seconds;
    int d = (int)(s / 86400);
    int h = (int)((s % 86400) / 3600);
    int m = (int)((s % 3600) / 60);
    if (d > 0)
        snprintf(buf, len, "up %dd %dh", d, h);
    else if (h > 0)
        snprintf(buf, len, "up %dh %dm", h, m);
    else
        snprintf(buf, len, "up %dm", m);
}

static void format_nodata(double last_seen, double now, char *buf, size_t len) {
    if (last_seen <= 0.0) {
        snprintf(buf, len, "NO DATA");
        return;
    }
    long age = (long)(now - last_seen);
    if (age < 0)
        age = 0;
    if (age < 60)
        snprintf(buf, len, "NO DATA %lds", age);
    else if (age < 3600)
        snprintf(buf, len, "NO DATA %ldm", age / 60);
    else if (age < 86400)
        snprintf(buf, len, "NO DATA %ldh", age / 3600);
    else
        snprintf(buf, len, "NO DATA %ldd", age / 86400);
}

/* Windows' "26H2"-style feature update. */
static bool is_feature_update(const char *t, size_t n) {
    return n == 4 && isdigit((unsigned char)t[0]) && isdigit((unsigned char)t[1]) &&
           (t[2] == 'H' || t[2] == 'h') && isdigit((unsigned char)t[3]);
}

/* A Windows build number: "26300.9818", "19045". The "11" of "Win 11" is not. */
static bool is_build(const char *t, size_t n) {
    bool dot = false;
    for (size_t i = 0; i < n; i++) {
        if (t[i] == '.')
            dot = true;
        else if (!isdigit((unsigned char)t[i]))
            return false;
    }
    return dot || n >= 4;
}

/* Append n bytes of t to out as a space-separated word, truncating at len. */
static void append_word(char *out, size_t len, size_t *pos, const char *t, size_t n) {
    if (*pos > 0 && *pos + 1 < len)
        out[(*pos)++] = ' ';
    while (n-- > 0 && *pos + 1 < len)
        out[(*pos)++] = *t++;
    out[*pos] = '\0';
}

void hostcols_short_os(const char *in, char *out, size_t len) {
    if (len == 0)
        return;
    out[0] = '\0';
    size_t pos = 0;
    bool windows = false, first = true;
    const char *p = in;
    int depth = 0;

    while (*p) {
        /* Skip whitespace and parenthesised spans, which may hold spaces. */
        if (isspace((unsigned char)*p) || *p == '(' || depth > 0) {
            if (*p == '(')
                depth++;
            else if (*p == ')' && depth > 0)
                depth--;
            p++;
            continue;
        }
        const char *t = p;
        while (*p && !isspace((unsigned char)*p) && *p != '(')
            p++;
        size_t n = (size_t)(p - t);

        if (first)
            windows = (n == 3 && strncmp(t, "Win", 3) == 0) ||
                      (n == 7 && strncmp(t, "Windows", 7) == 0);
        first = false;

        if (n == 9 && strncmp(t, "GNU/Linux", 9) == 0)
            continue;
        if (windows) {
            if (is_feature_update(t, n) || is_build(t, n))
                break;
            append_word(out, len, &pos, t, n);
            continue;
        }
        if (isdigit((unsigned char)*t)) {
            /* The version: at most two numeric parts, then nothing after it. */
            size_t v = 0;
            while (v < n && isdigit((unsigned char)t[v]))
                v++;
            if (v + 1 < n && t[v] == '.' && isdigit((unsigned char)t[v + 1])) {
                v++;
                while (v < n && isdigit((unsigned char)t[v]))
                    v++;
            }
            append_word(out, len, &pos, t, v);
            break;
        }
        append_word(out, len, &pos, t, n);
    }

    if (pos == 0)
        snprintf(out, len, "%s", in);
}

static hc_att_t worse(hc_att_t a, hc_att_t b) {
    return a > b ? a : b;
}

void hostcols_build(const client_info_t *c, double now, hc_host_t *out) {
    memset(out, 0, sizeof(*out));
    memcpy(out->host, c->hostname, strnlen(c->hostname, HOSTNAME_LEN - 1));
    hostcols_short_os(c->os_name, out->os, sizeof(out->os));

    out->nodata = client_presence(c) == CLIENT_PRESENCE_OFFLINE;
    if (out->nodata)
        format_nodata(c->last_seen_ts, now, out->status, sizeof(out->status));
    else
        format_uptime(c->uptime_seconds, out->status, sizeof(out->status));

    out->cpu = round_pct(c->cpu_pct);
    out->core = round_pct(c->top_core_pct);
    out->ram_pct = c->ram_total_mb ? round_pct(100.0f * c->ram_used_mb / c->ram_total_mb) : 0;
    out->ram_att = hostcols_ram_att(out->ram_pct);

    out->gpu = c->gpu.present;
    if (out->gpu) {
        out->gpu_pct = round_pct(c->gpu.compute_pct);
        snprintf(out->vram, sizeof(out->vram), "%.1f / %.1f", c->gpu.vram_used_mb / 1024.0f,
                 c->gpu.vram_total_mb / 1024.0f);
        out->vram_pct = c->gpu.vram_total_mb
                            ? round_pct(100.0f * c->gpu.vram_used_mb / c->gpu.vram_total_mb)
                            : 0;
    }

    /* Drives: one row for the fullest (the first reported wins a tie), and a
     * count of the others over the warn line. The border is judged on all. */
    int nd = c->disk_count;
    if (nd > MAX_DISKS)
        nd = MAX_DISKS;
    hc_att_t att = out->nodata ? HC_ATT_CRIT : HC_ATT_NONE;
    att = worse(att, out->ram_att);
    int worst = -1, over = 0;
    for (int i = 0; i < nd; i++) {
        hc_att_t da = hostcols_disk_att(round_pct(c->disks[i].pct));
        att = worse(att, da);
        if (da != HC_ATT_NONE)
            over++;
        if (worst < 0 || c->disks[i].pct > c->disks[worst].pct)
            worst = i;
    }
    if (worst >= 0) {
        hc_disk_t *d = &out->disk;
        memcpy(d->name, c->disks[worst].label, strnlen(c->disks[worst].label, LABEL_LEN - 1));
        d->pct = round_pct(c->disks[worst].pct);
        d->att = hostcols_disk_att(d->pct);
        out->has_disk = true;
        out->disk_more = over - (d->att != HC_ATT_NONE);
    }
    out->att = att;
}
