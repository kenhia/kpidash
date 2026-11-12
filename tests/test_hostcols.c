/* test_hostcols.c — the host columns widget's pure half (WI #4524, #4525):
 * the fill rule as a table, priority-then-hostname order, who gets a slot,
 * and the attention levels a column's border and rows are drawn in. */
#include "hostcols.h"

#include <stdio.h>
#include <string.h>

static int passed = 0;
static int failed = 0;

#define CHECK(expr)                                                                                \
    do {                                                                                           \
        if (!(expr)) {                                                                             \
            fprintf(stderr, "FAIL [%s:%d]: %s\n", __FILE__, __LINE__, #expr);                      \
            failed++;                                                                              \
        } else {                                                                                   \
            passed++;                                                                              \
        }                                                                                          \
    } while (0)

static void test_plan_table(void) {
    static const struct {
        int n, full, compact;
    } rows[] = {
        {0, 0, 0},   {1, 1, 0},   {7, 7, 0},    {10, 10, 0}, /* all full */
        {11, 9, 2},                                         /* Ken's check */
        {12, 9, 3},  {13, 8, 5},  {14, 8, 6},   {15, 7, 8},
        {20, 5, 15},                                        /* Ken's check */
        {25, 2, 23}, {28, 1, 27}, {29, 0, 29},  {30, 0, 30}, /* all compact */
        {31, 0, 30}, {64, 0, 30},                           /* capped at 30 */
    };
    for (size_t i = 0; i < sizeof(rows) / sizeof(rows[0]); i++) {
        int full = -1, compact = -1;
        hostcols_plan(rows[i].n, &full, &compact);
        if (full != rows[i].full || compact != rows[i].compact)
            fprintf(stderr, "  plan(%d) = %d full + %d compact, want %d + %d\n", rows[i].n, full,
                    compact, rows[i].full, rows[i].compact);
        CHECK(full == rows[i].full && compact == rows[i].compact);
    }
    /* Every plan fits: full columns plus compact columns never exceed ten. */
    for (int n = 0; n <= HOSTCOLS_MAX; n++) {
        int full, compact;
        hostcols_plan(n, &full, &compact);
        int ccols = (compact + HOSTCOLS_PER - 1) / HOSTCOLS_PER;
        CHECK(full + ccols <= HOSTCOLS_COLS);
        CHECK(full + compact == n);
    }
}

static client_info_t mk(const char *host) {
    client_info_t c;
    memset(&c, 0, sizeof(c));
    strncpy(c.hostname, host, HOSTNAME_LEN - 1);
    c.online = true;
    return c;
}

static void test_order(void) {
    char prio[3][HOSTNAME_LEN] = {"kai", "kubs0", "cleo"};
    registry_set_priority_clients(prio, 3);

    client_info_t in[6] = {mk("zeta"), mk("cleo"), mk("gratch"), mk("kai"), mk("alpha"),
                           mk("kubs0")};
    const client_info_t *out[6];
    int dropped = -1;
    int n = hostcols_select(in, 6, out, 6, &dropped);
    CHECK(n == 6 && dropped == 0);
    const char *want[6] = {"kai", "kubs0", "cleo", "alpha", "gratch", "zeta"};
    for (int i = 0; i < n; i++)
        CHECK(strcmp(out[i]->hostname, want[i]) == 0);

    /* Registration order must not matter: reversed input, same answer. */
    client_info_t rev[6];
    for (int i = 0; i < 6; i++)
        rev[i] = in[5 - i];
    n = hostcols_select(rev, 6, out, 6, NULL);
    for (int i = 0; i < n; i++)
        CHECK(strcmp(out[i]->hostname, want[i]) == 0);

    /* Over capacity: the first `max` by priority, the rest counted. */
    n = hostcols_select(in, 6, out, 4, &dropped);
    CHECK(n == 4 && dropped == 2);
    CHECK(strcmp(out[3]->hostname, "alpha") == 0);

    registry_set_priority_clients(prio, 0);
}

static void test_asleep_takes_no_slot(void) {
    client_info_t in[3] = {mk("kai"), mk("thrall"), mk("rpi53")};
    in[1].online = false;
    in[1].intermittent = true; /* asleep: hidden */
    in[2].online = false;      /* always-on and gone: shown, as NO DATA */
    const client_info_t *out[3];
    int n = hostcols_select(in, 3, out, 3, NULL);
    CHECK(n == 2);
    CHECK(strcmp(out[0]->hostname, "kai") == 0);
    CHECK(strcmp(out[1]->hostname, "rpi53") == 0);
    /* Awake again: it takes a slot back. */
    in[1].online = true;
    CHECK(hostcols_select(in, 3, out, 3, NULL) == 3);
}

static void test_thresholds(void) {
    CHECK(hostcols_ram_att(79) == HC_ATT_NONE);
    CHECK(hostcols_ram_att(80) == HC_ATT_WARN);
    CHECK(hostcols_ram_att(89) == HC_ATT_WARN);
    CHECK(hostcols_ram_att(90) == HC_ATT_CRIT);
    CHECK(hostcols_disk_att(63) == HC_ATT_NONE); /* the laptop drive: no border */
    CHECK(hostcols_disk_att(79) == HC_ATT_NONE);
    CHECK(hostcols_disk_att(80) == HC_ATT_WARN);
    CHECK(hostcols_disk_att(89) == HC_ATT_WARN);
    CHECK(hostcols_disk_att(90) == HC_ATT_CRIT);
}

static void add_disk(client_info_t *c, const char *label, float pct) {
    disk_entry_t *d = &c->disks[c->disk_count++];
    strncpy(d->label, label, LABEL_LEN - 1);
    d->pct = pct;
    d->total_gb = 100.0f;
    d->used_gb = pct;
}

static void test_build(void) {
    const double now = 1700000000.0;
    client_info_t c = mk("kai");
    strcpy(c.os_name, "Ubuntu 24.04");
    c.uptime_seconds = 21 * 86400 + 21 * 3600 + 5;
    c.cpu_pct = 3.4f;
    c.top_core_pct = 14.6f;
    c.ram_used_mb = 9500;
    c.ram_total_mb = 126464;
    c.gpu.present = true;
    c.gpu.compute_pct = 0.0f;
    c.gpu.vram_used_mb = 31027; /* 30.3 GB */
    c.gpu.vram_total_mb = 32563; /* 31.8 GB */
    add_disk(&c, "root", 29.0f);
    add_disk(&c, "ai", 35.0f);

    hc_host_t h;
    hostcols_build(&c, now, &h);
    CHECK(strcmp(h.host, "kai") == 0);
    CHECK(strcmp(h.status, "up 21d 21h") == 0);
    CHECK(!h.nodata);
    CHECK(h.att == HC_ATT_NONE);
    CHECK(h.cpu == 3 && h.core == 15);
    CHECK(h.ram_pct == 8 && h.ram_att == HC_ATT_NONE);
    CHECK(h.gpu && h.gpu_pct == 0);
    CHECK(strcmp(h.vram, "30.3 / 31.8") == 0);
    CHECK(h.vram_pct == 95);
    CHECK(h.ndisks == 2 && strcmp(h.disks[0].name, "root") == 0); /* original order kept */
    CHECK(h.worst == 1);

    /* VRAM at 95 % raises nothing; RAM at 92 % is critical. */
    c.ram_used_mb = 116000;
    hostcols_build(&c, now, &h);
    CHECK(h.ram_att == HC_ATT_CRIT && h.att == HC_ATT_CRIT);

    /* A drive at 85 % warns, and the drive carries the colour. */
    c.ram_used_mb = 9500;
    add_disk(&c, "src", 85.0f);
    hostcols_build(&c, now, &h);
    CHECK(h.att == HC_ATT_WARN);
    CHECK(h.disks[2].att == HC_ATT_WARN && h.worst == 2);

    /* More than five drives: the five fullest, fullest first. */
    client_info_t d = mk("kubsdb");
    float p[7] = {10, 50, 20, 70, 30, 60, 40};
    for (int i = 0; i < 7; i++) {
        char nm[8];
        snprintf(nm, sizeof(nm), "d%d", i);
        add_disk(&d, nm, p[i]);
    }
    hostcols_build(&d, now, &h);
    CHECK(h.ndisks == 5);
    CHECK(h.disks[0].pct == 70 && h.disks[4].pct == 30);
    CHECK(h.worst == 0);

    /* No GPU, no drives. */
    client_info_t e = mk("krgate");
    e.uptime_seconds = 3 * 3600 + 12 * 60;
    hostcols_build(&e, now, &h);
    CHECK(!h.gpu && h.ndisks == 0 && h.worst == -1);
    CHECK(strcmp(h.status, "up 3h 12m") == 0);
    e.uptime_seconds = 47 * 60;
    hostcols_build(&e, now, &h);
    CHECK(strcmp(h.status, "up 47m") == 0);
}

static void test_nodata(void) {
    const double now = 1700000000.0;
    client_info_t c = mk("rpi53");
    c.online = false;
    c.last_seen_ts = now - 4 * 60 - 10;
    hc_host_t h;
    hostcols_build(&c, now, &h);
    CHECK(h.nodata && h.att == HC_ATT_CRIT);
    CHECK(strcmp(h.status, "NO DATA 4m") == 0);
    c.last_seen_ts = now - 30;
    hostcols_build(&c, now, &h);
    CHECK(strcmp(h.status, "NO DATA 30s") == 0);
    c.last_seen_ts = now - 5 * 3600;
    hostcols_build(&c, now, &h);
    CHECK(strcmp(h.status, "NO DATA 5h") == 0);
    c.last_seen_ts = now - 3 * 86400;
    hostcols_build(&c, now, &h);
    CHECK(strcmp(h.status, "NO DATA 3d") == 0);
    /* Never seen this run (seeded from the admitted file): no age to give. */
    c.last_seen_ts = 0;
    hostcols_build(&c, now, &h);
    CHECK(strcmp(h.status, "NO DATA") == 0);
}

int main(void) {
    registry_init();
    test_plan_table();
    test_order();
    test_asleep_takes_no_slot();
    test_thresholds();
    test_build();
    test_nodata();
    printf("test_hostcols: %d passed, %d failed\n", passed, failed);
    return failed ? 1 : 0;
}
