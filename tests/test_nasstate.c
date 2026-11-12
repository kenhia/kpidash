/* test_nasstate.c — the NAS panel's model (WI #3956).
 *
 * Fed with the kdash-nas schema's own examples, read from the kdashdata
 * submodule, so a contract change shows up here. */
#include "nasstate.h"

#include <cjson/cJSON.h>
#include <stdio.h>
#include <stdlib.h>
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

static char *slurp(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *buf = malloc((size_t)n + 1);
    if (buf && fread(buf, 1, (size_t)n, f) != (size_t)n) {
        free(buf);
        buf = NULL;
    }
    if (buf) buf[n] = '\0';
    fclose(f);
    return buf;
}

/* The schema's examples[i], re-serialised. Caller frees. */
static char *schema_example(int i) {
    char *text = slurp(KDASH_NAS_SCHEMA);
    if (!text) return NULL;
    cJSON *root = cJSON_Parse(text);
    free(text);
    cJSON *ex = cJSON_GetArrayItem(cJSON_GetObjectItemCaseSensitive(root, "examples"), i);
    char *out = ex ? cJSON_PrintUnformatted(ex) : NULL;
    cJSON_Delete(root);
    return out;
}

static void test_schema_example_parses(void) {
    char *json = schema_example(0);
    CHECK(json != NULL);
    nas_record_t r;
    CHECK(nas_parse(json, &r) == 0);
    CHECK(r.ts == 1793163800.2);
    CHECK(r.has_collected && r.collected_ts == 1793163791.0);
    CHECK(!r.source_stale);
    CHECK(r.npools == 1 && strcmp(r.pools[0].name, "nas") == 0);
    CHECK(strcmp(r.pools[0].health, "ONLINE") == 0);
    CHECK(r.pools[0].used_bytes == 976605544448LL);
    CHECK(r.pools[0].errors == 0);
    CHECK(r.pools[0].has_scrub && strcmp(r.pools[0].scrub_state, "finished") == 0);
    CHECK(r.ndisks == 2);
    const nas_disk_t *nv = &r.disks[0], *hd = &r.disks[1];
    CHECK(strcmp(nv->kind, "nvme") == 0 && nv->reallocated_sectors == -1);
    CHECK(nv->percentage_used == 0 && nv->smart_passed == 1);
    CHECK(strcmp(hd->power_state, "standby") == 0 && hd->reallocated_sectors == 8);
    CHECK(hd->percentage_used == -1);
    /* Standby and a constant reallocated count are shown, not faults. */
    CHECK(nas_disk_level(hd) == NAS_NEUTRAL);
    CHECK(nas_disk_level(nv) == NAS_OK);
    CHECK(nas_overall_level(&r) == NAS_OK);
    free(json);
}

/* "collector down": still a record, and it is a warning. */
static void test_collector_down_example(void) {
    char *json = schema_example(1);
    nas_record_t r;
    CHECK(json && nas_parse(json, &r) == 0);
    CHECK(!r.has_collected && r.source_stale && r.npools == 0 && r.ndisks == 0);
    CHECK(nas_overall_level(&r) == NAS_WARN);
    free(json);
}

static void test_rejects_non_records(void) {
    nas_record_t r;
    CHECK(nas_parse(NULL, &r) == -1);
    CHECK(nas_parse("garbage", &r) == -1);
    CHECK(nas_parse("[1,2]", &r) == -1);
    CHECK(nas_parse("{\"pools\":[]}", &r) == -1); /* no ts */
    /* An unreadable source_stale reads stale, never fresh. */
    CHECK(nas_parse("{\"ts\":5,\"collected_ts\":4}", &r) == 0);
    CHECK(r.source_stale);
}

static void test_feed_freshness(void) {
    nas_reset();
    CHECK(nas_feed(100) == NAS_FEED_NONE);
    CHECK(!nas_has_record());
    nas_observe("{\"ts\":100,\"collected_ts\":99,\"source_stale\":false,\"pools\":[],"
                "\"disks\":[]}",
                101);
    CHECK(nas_has_record());
    CHECK(nas_feed(101) == NAS_FEED_LIVE);
    CHECK(nas_feed(116) == NAS_FEED_STALE); /* key present but record too old */
    /* Key gone: the last record stays, read stale. */
    nas_observe(NULL, 105);
    CHECK(nas_feed(105) == NAS_FEED_STALE);
    CHECK(nas_record()->ts == 100);
    /* An unreadable payload is not a fresh one either. */
    nas_observe("{nope", 106);
    CHECK(nas_feed(106) == NAS_FEED_STALE);
}

static void test_levels(void) {
    nas_pool_t p = {.health = "ONLINE", .errors = 0};
    CHECK(nas_pool_level(&p) == NAS_OK);
    p.errors = 3;
    CHECK(nas_pool_level(&p) == NAS_WARN);
    p.errors = 0;
    strcpy(p.health, "DEGRADED");
    CHECK(nas_pool_level(&p) == NAS_WARN);
    strcpy(p.health, "FAULTED");
    CHECK(nas_pool_level(&p) == NAS_CRIT);

    nas_disk_t d = {.kind = "hdd", .power_state = "active", .smart_passed = 1,
                    .reallocated_sectors = 0, .pending_sectors = 0,
                    .percentage_used = -1, .media_errors = -1, .has_temp = true,
                    .temp_c = 34};
    CHECK(nas_disk_level(&d) == NAS_OK);
    d.pending_sectors = 1;
    CHECK(nas_disk_level(&d) == NAS_WARN);
    d.pending_sectors = 0;
    d.temp_c = 61;
    CHECK(nas_disk_level(&d) == NAS_CRIT);
    d.temp_c = 34;
    d.smart_passed = 0;
    CHECK(nas_disk_level(&d) == NAS_CRIT);
    /* Standby never hides a failed SMART verdict. */
    strcpy(d.power_state, "standby");
    CHECK(nas_disk_level(&d) == NAS_CRIT);

    nas_disk_t n = {.kind = "nvme", .power_state = "active", .smart_passed = 1,
                    .reallocated_sectors = -1, .pending_sectors = -1,
                    .percentage_used = 85, .media_errors = 0, .has_temp = true,
                    .temp_c = 65};
    CHECK(nas_disk_level(&n) == NAS_WARN); /* wear, not the 65 °C */
}

static void test_formatting(void) {
    char b[32];
    nas_format_bytes(976605544448LL, b, sizeof(b));
    CHECK(strcmp(b, "977 GB") == 0);
    nas_format_bytes(5858335391744LL, b, sizeof(b));
    CHECK(strcmp(b, "5.9 TB") == 0);
    nas_format_bytes(-1, b, sizeof(b));
    CHECK(strcmp(b, "?") == 0);
    nas_format_age(45, b, sizeof(b));
    CHECK(strcmp(b, "45 s") == 0);
    nas_format_age(3 * 86400 + 5, b, sizeof(b));
    CHECK(strcmp(b, "3 d") == 0);
}

static void test_capacity(void) {
    char json[8192];
    size_t used = (size_t)snprintf(json, sizeof(json), "{\"ts\":1,\"disks\":[");
    for (int i = 0; i < NAS_DISKS_MAX + 5; i++)
        used += (size_t)snprintf(json + used, sizeof(json) - used, "%s{\"device\":\"/dev/x%d\"}",
                                 i ? "," : "", i);
    snprintf(json + used, sizeof(json) - used, "]}");
    nas_record_t r;
    CHECK(nas_parse(json, &r) == 0);
    CHECK(r.ndisks == NAS_DISKS_MAX);
}

/* ---- Sprint 033: disk use, order and the headline ---- */

/* gratch's own record, captured from rpi53 on 2026-11-12: the model strings
 * nas_disk_uses.h matches on, in the order the producer sends them. */
static bool load_live(nas_record_t *r) {
    char *json = slurp(NAS_LIVE_FIXTURE);
    bool ok = json && nas_parse(json, r) == 0;
    free(json);
    return ok;
}

static nas_disk_t *disk_named(nas_record_t *r, const char *dev) {
    for (int i = 0; i < r->ndisks; i++)
        if (strcmp(nas_short_device(r->disks[i].device), dev) == 0) return &r->disks[i];
    return NULL;
}

static void test_live_order_and_uses(void) {
    nas_record_t r;
    CHECK(load_live(&r));
    CHECK(r.ndisks == 8);
    int idx[NAS_DISKS_MAX];
    int n = nas_disk_order(&r, idx, 8);
    CHECK(n == 8);
    /* Ken's order, and the use label above each. */
    static const char *const dev[] = {"nvme2", "nvme0", "nvme1", "nvme3",
                                      "sda",   "sdb",   "sdc",   "sdd"};
    static const char *const use[] = {"root",  "nas pool", "nas pool", "nas pool",
                                      "cache", "cold",     "cold",     "cold"};
    for (int i = 0; i < n && i < 8; i++) {
        const nas_disk_t *d = &r.disks[idx[i]];
        CHECK(strcmp(nas_short_device(d->device), dev[i]) == 0);
        CHECK(strcmp(nas_use_label(nas_disk_use(d)), use[i]) == 0);
    }
}

/* nvmeN is not stable across boots: the model, not the name, holds a slot. */
static void test_order_follows_model_not_name(void) {
    nas_record_t r;
    CHECK(load_live(&r));
    nas_disk_t *root = disk_named(&r, "nvme2"), *pool = disk_named(&r, "nvme0");
    CHECK(root && pool);
    if (!root || !pool) return;
    strcpy(root->device, "/dev/nvme0");
    strcpy(pool->device, "/dev/nvme2");
    int idx[NAS_DISKS_MAX];
    nas_disk_order(&r, idx, NAS_DISKS_MAX);
    CHECK(strcmp(r.disks[idx[0]].device, "/dev/nvme0") == 0);
    CHECK(nas_disk_use(&r.disks[idx[0]]) == NAS_USE_ROOT);
    CHECK(strcmp(r.disks[idx[1]].device, "/dev/nvme2") == 0);
}

/* A disk the table does not know: no label, after every listed one. */
static void test_unlisted_disk_sorts_last(void) {
    nas_record_t r;
    CHECK(load_live(&r));
    nas_disk_t *sda = disk_named(&r, "sda");
    CHECK(sda != NULL);
    if (!sda) return;
    strcpy(sda->model, "Some New Disk 8TB");
    int idx[NAS_DISKS_MAX];
    int n = nas_disk_order(&r, idx, NAS_DISKS_MAX);
    CHECK(n == 8);
    CHECK(&r.disks[idx[7]] == sda);
    CHECK(nas_disk_use(sda) == NAS_USE_NONE);
    CHECK(strcmp(nas_use_label(NAS_USE_NONE), "") == 0);
    /* `max` caps the list without disturbing the order. */
    CHECK(nas_disk_order(&r, idx, 3) == 3);
    CHECK(strcmp(nas_short_device(r.disks[idx[0]].device), "nvme2") == 0);
}

/* The four states the card is judged in: ok, disk warning, pool degraded,
 * stale. */
static void test_headline_states(void) {
    nas_record_t r;
    CHECK(load_live(&r));
    double now = r.ts + 2;
    char head[64], sub[128];
    nas_level_t lvl = NAS_CRIT;

    nas_headline(&r, NAS_FEED_LIVE, now, head, sizeof(head), sub, sizeof(sub), &lvl);
    CHECK(strcmp(head, "NAS OK") == 0);
    CHECK(strcmp(sub, "8 disks \xc2\xb7 scrub 4 d ago \xc2\xb7 0 errors") == 0);
    CHECK(lvl == NAS_OK);

    nas_record_t w = r;
    disk_named(&w, "sdc")->pending_sectors = 2;
    nas_headline(&w, NAS_FEED_LIVE, now, head, sizeof(head), sub, sizeof(sub), &lvl);
    CHECK(strcmp(head, "sdc PENDING 2") == 0);
    CHECK(strcmp(sub, "1 disk needs a look \xc2\xb7 scrub 4 d ago") == 0);
    CHECK(lvl == NAS_WARN);

    /* The pool outranks the disk that failed it. */
    nas_record_t c = r;
    strcpy(c.pools[0].health, "DEGRADED");
    disk_named(&c, "sdd")->smart_passed = 0;
    nas_headline(&c, NAS_FEED_LIVE, now, head, sizeof(head), sub, sizeof(sub), &lvl);
    CHECK(strcmp(head, "nas DEGRADED") == 0);
    CHECK(strcmp(sub, "1 disk needs a look \xc2\xb7 scrub 4 d ago") == 0);
    CHECK(lvl == NAS_CRIT);

    nas_headline(&r, NAS_FEED_STALE, r.ts + 240, head, sizeof(head), sub, sizeof(sub), &lvl);
    CHECK(strcmp(head, "STALE \xc2\xb7 4 m") == 0);
    CHECK(strcmp(sub, "last values shown \xc2\xb7 no data for 4 m") == 0);
}

static void test_headline_other_causes(void) {
    nas_record_t r;
    CHECK(load_live(&r));
    double now = r.ts + 2;
    char head[64], sub[128];
    nas_level_t lvl;

    nas_record_t e = r;
    e.pools[0].errors = 3;
    nas_headline(&e, NAS_FEED_LIVE, now, head, sizeof(head), sub, sizeof(sub), &lvl);
    CHECK(strcmp(head, "nas 3 ERRORS") == 0);
    CHECK(strcmp(sub, "8 disks \xc2\xb7 scrub 4 d ago \xc2\xb7 3 errors") == 0);

    nas_record_t h = r;
    disk_named(&h, "nvme2")->temp_c = 81;
    nas_headline(&h, NAS_FEED_LIVE, now, head, sizeof(head), sub, sizeof(sub), &lvl);
    CHECK(strcmp(head, "nvme2 HOT 81\xc2\xb0") == 0);
    CHECK(lvl == NAS_CRIT);

    nas_record_t s = r;
    s.source_stale = true;
    nas_headline(&s, NAS_FEED_LIVE, now, head, sizeof(head), sub, sizeof(sub), &lvl);
    CHECK(strcmp(head, "SOURCE STALE") == 0);
    CHECK(lvl == NAS_WARN);

    /* sdc's constant reallocated count and the standby HDDs are not news. */
    char why[32];
    nas_disk_reason(disk_named(&r, "sdc"), why, sizeof(why));
    CHECK(why[0] == '\0');
}

int main(void) {
    test_schema_example_parses();
    test_collector_down_example();
    test_rejects_non_records();
    test_feed_freshness();
    test_levels();
    test_formatting();
    test_capacity();
    test_live_order_and_uses();
    test_order_follows_model_not_name();
    test_unlisted_disk_sorts_last();
    test_headline_states();
    test_headline_other_causes();
    fprintf(stderr, "test_nasstate: %d passed, %d failed\n", passed, failed);
    return failed == 0 ? 0 : 1;
}
