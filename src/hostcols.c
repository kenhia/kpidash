/**
 * hostcols.c — Fill rule, host order and display model for the host columns
 * widget (WI #4524, #4525). Pure: see hostcols.h.
 */
#include "hostcols.h"

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

static hc_att_t worse(hc_att_t a, hc_att_t b) {
    return a > b ? a : b;
}

static int cmp_disk_desc(const void *x, const void *y) {
    const disk_entry_t *a = *(const disk_entry_t *const *)x;
    const disk_entry_t *b = *(const disk_entry_t *const *)y;
    return (a->pct < b->pct) - (a->pct > b->pct);
}

void hostcols_build(const client_info_t *c, double now, hc_host_t *out) {
    memset(out, 0, sizeof(*out));
    memcpy(out->host, c->hostname, strnlen(c->hostname, HOSTNAME_LEN - 1));
    memcpy(out->os, c->os_name, strnlen(c->os_name, OS_NAME_LEN - 1));

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

    /* Drives: as reported, unless there are more than five — then the five
     * fullest, fullest first, so the one that matters is never the one cut. */
    int nd = c->disk_count;
    if (nd > MAX_DISKS)
        nd = MAX_DISKS;
    if (nd < 0)
        nd = 0;
    const disk_entry_t *ds[MAX_DISKS];
    for (int i = 0; i < nd; i++)
        ds[i] = &c->disks[i];
    if (nd > HOSTCOLS_DISKS) {
        qsort(ds, (size_t)nd, sizeof(ds[0]), cmp_disk_desc);
        nd = HOSTCOLS_DISKS;
    }
    out->ndisks = nd;
    out->worst = -1;
    hc_att_t att = out->nodata ? HC_ATT_CRIT : HC_ATT_NONE;
    att = worse(att, out->ram_att);
    for (int i = 0; i < nd; i++) {
        hc_disk_t *d = &out->disks[i];
        memcpy(d->name, ds[i]->label, strnlen(ds[i]->label, LABEL_LEN - 1));
        d->pct = round_pct(ds[i]->pct);
        d->att = hostcols_disk_att(d->pct);
        att = worse(att, d->att);
        if (out->worst < 0 || ds[i]->pct > ds[out->worst]->pct)
            out->worst = i;
    }
    out->att = att;
}
