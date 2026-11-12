/* render_host_columns.c — offline render of the host columns widget to BMP
 * (WI #4524/#4525). Not a test: a review tool, built with the full native
 * tree (it needs LVGL), so a layout can be looked at without a Pi.
 *
 *   render_host_columns <out.bmp> <n> [alert]     mock-up fleet, first n hosts
 *   render_host_columns <out.bmp> -f <hosts.txt>  hosts from a file
 *
 * The mock-up fleet mirrors sprints/planning/hosts-columns-mockups.html's
 * `hostsData`: array order is priority order, thrall and kit are asleep (and
 * so take no slot), and `alert` applies the same five faults. The file form is
 * one host per line, '|'-separated:
 *
 *   host|os|uptime_s|online|intermittent|age_s|cpu|core|ram_used_mb|
 *   ram_total_mb|vram_used_mb|vram_total_mb|gpu_pct|label:pct,label:pct...
 *
 * with vram_total_mb 0 for no GPU. Sparklines are synthesised. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "bmp_write.h"
#include "hostcols.h"
#include "hosthist.h"
#include "lvgl.h"
#include "registry.h"
#include "widgets/host_columns.h"

static uint32_t g_rng = 7;
static float rnd(void) {
    g_rng = g_rng * 1664525u + 1013904223u;
    return (float)(g_rng >> 8) / 16777216.0f;
}

static client_info_t g_hosts[MAX_CLIENTS];
static int g_n = 0;

static client_info_t *add(const char *name, const char *os, float up_s, float cpu, float core,
                          float ram_used_gb, float ram_total_gb) {
    client_info_t *c = &g_hosts[g_n++];
    memset(c, 0, sizeof(*c));
    snprintf(c->hostname, HOSTNAME_LEN, "%s", name);
    snprintf(c->os_name, OS_NAME_LEN, "%s", os);
    c->online = true;
    c->uptime_seconds = up_s;
    c->cpu_pct = cpu;
    c->top_core_pct = core;
    c->ram_used_mb = (uint32_t)(ram_used_gb * 1024);
    c->ram_total_mb = (uint32_t)(ram_total_gb * 1024);
    return c;
}

static void gpu(client_info_t *c, float used_gb, float total_gb, float pct) {
    c->gpu.present = true;
    c->gpu.vram_used_mb = (uint32_t)(used_gb * 1024);
    c->gpu.vram_total_mb = (uint32_t)(total_gb * 1024);
    c->gpu.compute_pct = pct;
}

static void disk(client_info_t *c, const char *label, float used, float total) {
    disk_entry_t *d = &c->disks[c->disk_count++];
    snprintf(d->label, LABEL_LEN, "%s", label);
    d->used_gb = used;
    d->total_gb = total;
    d->pct = total > 0 ? 100.0f * used / total : 0;
}

static void asleep(client_info_t *c) {
    c->online = false;
    c->intermittent = true;
}

#define D 86400.0f
#define H 3600.0f

static void mock_fleet(bool alert) {
    client_info_t *c;
    c = add("kai", "Ubuntu 24.04", 21 * D + 21 * H, 3, 14, 9.3f, 123.5f);
    gpu(c, 30.3f, 31.8f, 0);
    disk(c, "root", 535, 1800);
    disk(c, "ai", 645, 1800);
    if (alert)
        c->ram_used_mb = (uint32_t)(118.6f * 1024);
    c = add("kubs0", "Ubuntu 24.04", 24 * D + 22 * H, 2, 9, 11.5f, 125.6f);
    gpu(c, 13.4f, 16.0f, 0);
    disk(c, "root", 130, 1800);
    disk(c, "ai", 60.2f, 1800);
    disk(c, "src", alert ? 1548 : 378, 1800);
    c = add("cleo", "Win 11 Pro", 5 * D + 20 * H, 18, 69, 21.4f, 31.4f);
    gpu(c, 2.6f, 7.7f, 5);
    disk(c, "C", 623, 1800);
    disk(c, "D", 241, 931);
    disk(c, "F", 0, 120);
    c = add("kubsdb", "Ubuntu 24.04", 13 * D + 21 * H, 1, 5, 2.5f, 62.7f);
    disk(c, "root", 30.7f, 455);
    disk(c, "datastore", 9, 916);
    disk(c, "gratch", 69.2f, 4500);
    c = add("gratch", "Ubuntu 26.04", 19 * D + 4 * H, 1, 6, 12.4f, 31.3f);
    disk(c, "root", 13, 913);
    disk(c, "nas", 991, 5900);
    c = add("kimac", "macOS 27.0", 35 * D + 14 * H, 2, 4, 3.2f, 8);
    disk(c, "data", 45.3f, 460);
    c = add("rpi53", "Debian 13", 1 * H + 41 * 60, 12, 48, 0.45f, 7.9f);
    disk(c, "root", 8.8f, 468);
    disk(c, "firmware", 0.1f, 0.5f);
    if (alert) {
        c->online = false;
        c->last_seen_ts = (double)time(NULL) - 4 * 60;
    }
    c = add("krgate", "Debian 13", 11 * D + 2 * H, 1, 3, 0.6f, 3.8f);
    disk(c, "root", 6.1f, 58);
    c = add("komarchy", "Omarchy", 3 * H + 12 * 60, 4, 22, 5.1f, 15.5f);
    disk(c, "root", 88, 476);
    c = add("thrall", "Arch", 0, 0, 0, 0, 62.7f);
    gpu(c, 0, 24, 0);
    disk(c, "root", 212, 931);
    asleep(c);
    c = add("kit", "Win 11 Arm", 0, 0, 0, 0, 31.6f);
    disk(c, "C", 141, 476);
    asleep(c);
    c = add("ksandbox", "Ubuntu 24.04", 2 * H + 5 * 60, 6, 31, 1.9f, 7.8f);
    disk(c, "root", alert ? 59.5f : 21, 64);
    c = add("kbook2", "Win 11 Pro", 47 * 60, 9, 40, alert ? 13.4f : 9.8f, 15.8f);
    gpu(c, 1.1f, 6, 2);
    disk(c, "C", 301, 476);

    static const char *fill[] = {"rpidash3", "deskdash", "kwork",   "krpi4",   "kvm1",
                                 "kvm2",     "kvm3",     "ktest1",  "ktest2",  "ktest3",
                                 "ktest4",   "ktest5",   "ktest6",  "ktest7",  "ktest8",
                                 "ktest9",   "ktest10",  "ktest11", "ktest12"};
    static const float tots[] = {3.8f, 7.8f, 15.5f, 31.3f}, dts[] = {58, 117, 476};
    for (size_t i = 0; i < sizeof(fill) / sizeof(fill[0]) && g_n < MAX_CLIENTS; i++) {
        float tot = tots[(int)(rnd() * 4)], dt = dts[(int)(rnd() * 3)];
        c = add(fill[i], "Debian 13", (1 + (int)(rnd() * 30)) * D + (int)(rnd() * 24) * H,
                roundf(rnd() * 8), roundf(8 + rnd() * 30), tot * (0.15f + rnd() * 0.4f), tot);
        disk(c, "root", roundf(dt * (0.1f + rnd() * 0.5f)), dt);
    }
}

static int load_file(const char *path) {
    FILE *f = fopen(path, "r");
    if (!f)
        return -1;
    char line[1024];
    double now = (double)time(NULL);
    while (fgets(line, sizeof(line), f) && g_n < MAX_CLIENTS) {
        line[strcspn(line, "\n")] = '\0';
        char *fld[14] = {0};
        int nf = 0;
        for (char *p = line; nf < 14; nf++) {
            fld[nf] = p;
            char *bar = strchr(p, '|');
            if (!bar) {
                nf++;
                break;
            }
            *bar = '\0';
            p = bar + 1;
        }
        if (nf < 13)
            continue;
        client_info_t *c = add(fld[0], fld[1], (float)atof(fld[2]), (float)atof(fld[6]),
                               (float)atof(fld[7]), 0, 0);
        c->online = atoi(fld[3]) != 0;
        c->intermittent = atoi(fld[4]) != 0;
        c->last_seen_ts = now - atof(fld[5]);
        c->ram_used_mb = (uint32_t)atol(fld[8]);
        c->ram_total_mb = (uint32_t)atol(fld[9]);
        if (atol(fld[11]) > 0) {
            c->gpu.present = true;
            c->gpu.vram_used_mb = (uint32_t)atol(fld[10]);
            c->gpu.vram_total_mb = (uint32_t)atol(fld[11]);
            c->gpu.compute_pct = (float)atof(fld[12]);
        }
        for (char *tok = nf > 13 ? strtok(fld[13], ",") : NULL; tok && c->disk_count < MAX_DISKS;
             tok = strtok(NULL, ",")) {
            char *colon = strrchr(tok, ':');
            if (!colon)
                continue;
            *colon = '\0';
            disk(c, tok, (float)atof(colon + 1), 100);
        }
    }
    fclose(f);
    return 0;
}

/* Ten minutes of plausible history per host, like the mock-up's: a baseline
 * near the current value with the occasional spike. */
static void synth_history(void) {
    double now = (double)time(NULL);
    for (int i = 0; i < g_n; i++) {
        client_info_t *c = &g_hosts[i];
        if (client_presence(c) == CLIENT_PRESENCE_ASLEEP)
            continue;
        g_rng = 100u + (uint32_t)i;
        int len = client_presence(c) == CLIENT_PRESENCE_OFFLINE ? HOSTHIST_LEN - 60 : HOSTHIST_LEN;
        for (int k = 0; k < len; k++) {
            float sp = rnd() < 0.07f ? 15 + rnd() * 45 : 0;
            float a = fmaxf(0, c->cpu_pct + rnd() * 4 - 2 + sp * 0.35f);
            float gap = fmaxf(2, c->top_core_pct - c->cpu_pct);
            float core = fminf(100, a + gap * (0.5f + rnd() * 0.6f) + sp * 0.6f);
            float g = 0;
            if (c->gpu.present)
                g = (i == 0 && k > 70 && k < 92) ? 70 + rnd() * 25
                    : rnd() < 0.04f             ? 10 + rnd() * 20
                                                : c->gpu.compute_pct + rnd() * 0.6f;
            hosthist_record(c->hostname, now - (HOSTHIST_LEN - k) * HOSTHIST_PERIOD_S,
                            fminf(100, a), core, g);
        }
    }
}

static void flush_cb(lv_display_t *d, const lv_area_t *a, uint8_t *px) {
    (void)a;
    (void)px;
    lv_display_flush_ready(d);
}

int main(int argc, char **argv) {
    if (argc < 3) {
        fprintf(stderr, "usage: %s out.bmp <n> [alert] | out.bmp -f hosts.txt\n", argv[0]);
        return 2;
    }
    registry_init();
    int want;
    if (strcmp(argv[2], "-f") == 0) {
        if (argc < 4 || load_file(argv[3]) != 0) {
            fprintf(stderr, "cannot read host file\n");
            return 1;
        }
        want = g_n;
        char prio[3][HOSTNAME_LEN] = {"kai", "kubs0", "cleo"}; /* rpi53's config.env */
        registry_set_priority_clients(prio, 3);
    } else {
        mock_fleet(argc > 3 && strcmp(argv[3], "alert") == 0);
        /* The mock-up's array order is its priority order. */
        static char prio[MAX_CLIENTS][HOSTNAME_LEN];
        for (int i = 0; i < g_n; i++)
            memcpy(prio[i], g_hosts[i].hostname, HOSTNAME_LEN);
        registry_set_priority_clients(prio, g_n);
        want = atoi(argv[2]);
    }
    synth_history();

    lv_init();
    lv_display_t *disp = lv_display_create(1896, 628);
    static uint8_t buf[1896 * 64 * 4];
    lv_display_set_buffers(disp, buf, NULL, sizeof(buf), LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_flush_cb(disp, flush_cb);
    lv_obj_t *scr = lv_screen_active();
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x11111B), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);

    lv_obj_t *w = host_columns_create(scr, 0, 0);
    const client_info_t *sel[HOSTCOLS_MAX];
    int dropped = 0;
    int ns = hostcols_select(g_hosts, g_n, sel, want < HOSTCOLS_MAX ? want : HOSTCOLS_MAX,
                             &dropped);
    static hc_host_t models[HOSTCOLS_MAX];
    double now = (double)time(NULL);
    for (int i = 0; i < ns; i++)
        hostcols_build(sel[i], now, &models[i]);
    host_columns_update(w, models, ns);
    int full, compact;
    hostcols_plan(ns, &full, &compact);
    printf("%d hosts shown: %d full + %d compact\n", ns, full, compact);

    lv_draw_buf_t *snap = lv_snapshot_take(scr, LV_COLOR_FORMAT_XRGB8888);
    if (!snap) {
        fprintf(stderr, "snapshot failed\n");
        return 1;
    }
    FILE *f = fopen(argv[1], "wb");
    bool ok = f && bmp_write_xrgb8888(f, snap->data, (int)snap->header.w, (int)snap->header.h,
                                      (int)snap->header.stride);
    if (f)
        fclose(f);
    lv_draw_buf_destroy(snap);
    return ok ? 0 : 1;
}
