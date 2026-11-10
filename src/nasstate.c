/* nasstate.c — kdash:nas:{host} v1 model for the NAS big panel (WI #3956).
 * See nasstate.h for the freshness rules. */
#include "nasstate.h"

#include <cjson/cJSON.h>
#include <stdio.h>
#include <string.h>

static nas_record_t g_rec;
static bool g_has_rec = false;
static bool g_present = false; /* key present and readable on the last poll */

void nas_reset(void) {
    memset(&g_rec, 0, sizeof(g_rec));
    g_has_rec = false;
    g_present = false;
}

static void copy_str(char *dst, size_t n, const cJSON *item) {
    dst[0] = '\0';
    if (cJSON_IsString(item) && item->valuestring) {
        strncpy(dst, item->valuestring, n - 1);
        dst[n - 1] = '\0';
    }
}

/* -1 for null, absent, negative or not a number. */
static int64_t int_or_null(const cJSON *item) {
    if (!cJSON_IsNumber(item) || item->valuedouble < 0) return -1;
    return (int64_t)item->valuedouble;
}

static double num_or_zero(const cJSON *item) {
    return cJSON_IsNumber(item) ? item->valuedouble : 0.0;
}

static void parse_pool(const cJSON *o, nas_pool_t *p) {
    memset(p, 0, sizeof(*p));
    copy_str(p->name, sizeof(p->name), cJSON_GetObjectItemCaseSensitive(o, "name"));
    copy_str(p->health, sizeof(p->health), cJSON_GetObjectItemCaseSensitive(o, "health"));
    p->used_bytes = int_or_null(cJSON_GetObjectItemCaseSensitive(o, "used_bytes"));
    p->avail_bytes = int_or_null(cJSON_GetObjectItemCaseSensitive(o, "avail_bytes"));
    p->errors = int_or_null(cJSON_GetObjectItemCaseSensitive(o, "errors"));
    const cJSON *s = cJSON_GetObjectItemCaseSensitive(o, "scrub");
    if (cJSON_IsObject(s)) {
        p->has_scrub = true;
        copy_str(p->scrub_state, sizeof(p->scrub_state),
                 cJSON_GetObjectItemCaseSensitive(s, "state"));
        p->scrub_end_ts = num_or_zero(cJSON_GetObjectItemCaseSensitive(s, "end_ts"));
        p->scrub_errors = int_or_null(cJSON_GetObjectItemCaseSensitive(s, "errors"));
    }
}

static void parse_disk(const cJSON *o, nas_disk_t *d) {
    memset(d, 0, sizeof(*d));
    copy_str(d->device, sizeof(d->device), cJSON_GetObjectItemCaseSensitive(o, "device"));
    copy_str(d->model, sizeof(d->model), cJSON_GetObjectItemCaseSensitive(o, "model"));
    copy_str(d->kind, sizeof(d->kind), cJSON_GetObjectItemCaseSensitive(o, "kind"));
    copy_str(d->power_state, sizeof(d->power_state),
             cJSON_GetObjectItemCaseSensitive(o, "power_state"));
    const cJSON *sp = cJSON_GetObjectItemCaseSensitive(o, "smart_passed");
    d->smart_passed = cJSON_IsBool(sp) ? (cJSON_IsTrue(sp) ? 1 : 0) : -1;
    d->smart_ts = num_or_zero(cJSON_GetObjectItemCaseSensitive(o, "smart_ts"));
    const cJSON *t = cJSON_GetObjectItemCaseSensitive(o, "temp_c");
    d->has_temp = cJSON_IsNumber(t);
    d->temp_c = d->has_temp ? t->valuedouble : 0.0;
    d->reallocated_sectors = int_or_null(cJSON_GetObjectItemCaseSensitive(o, "reallocated_sectors"));
    d->pending_sectors = int_or_null(cJSON_GetObjectItemCaseSensitive(o, "pending_sectors"));
    d->percentage_used = int_or_null(cJSON_GetObjectItemCaseSensitive(o, "percentage_used"));
    d->media_errors = int_or_null(cJSON_GetObjectItemCaseSensitive(o, "media_errors"));
}

int nas_parse(const char *json, nas_record_t *out) {
    if (!json || !out) return -1;
    cJSON *root = cJSON_Parse(json);
    if (!root) return -1;
    const cJSON *ts = cJSON_GetObjectItemCaseSensitive(root, "ts");
    if (!cJSON_IsObject(root) || !cJSON_IsNumber(ts)) {
        cJSON_Delete(root);
        return -1;
    }
    memset(out, 0, sizeof(*out));
    out->ts = ts->valuedouble;
    const cJSON *c = cJSON_GetObjectItemCaseSensitive(root, "collected_ts");
    out->has_collected = cJSON_IsNumber(c);
    out->collected_ts = out->has_collected ? c->valuedouble : 0.0;
    /* Anything but an explicit false is stale: a field this build cannot
     * read must not read as fresh. */
    out->source_stale = !cJSON_IsFalse(cJSON_GetObjectItemCaseSensitive(root, "source_stale"));

    const cJSON *item;
    cJSON_ArrayForEach(item, cJSON_GetObjectItemCaseSensitive(root, "pools")) {
        if (out->npools >= NAS_POOLS_MAX) break;
        if (cJSON_IsObject(item)) parse_pool(item, &out->pools[out->npools++]);
    }
    cJSON_ArrayForEach(item, cJSON_GetObjectItemCaseSensitive(root, "disks")) {
        if (out->ndisks >= NAS_DISKS_MAX) break;
        if (cJSON_IsObject(item)) parse_disk(item, &out->disks[out->ndisks++]);
    }
    cJSON_Delete(root);
    return 0;
}

void nas_observe(const char *json, double now) {
    (void)now;
    nas_record_t rec;
    if (json && nas_parse(json, &rec) == 0) {
        g_rec = rec;
        g_has_rec = true;
        g_present = true;
        return;
    }
    /* Absent or unreadable: keep the last record, read stale. */
    g_present = false;
}

bool nas_has_record(void) { return g_has_rec; }
const nas_record_t *nas_record(void) { return &g_rec; }

nas_feed_t nas_feed(double now) {
    if (!g_has_rec) return NAS_FEED_NONE;
    if (!g_present || now - g_rec.ts > NAS_FRESH_S) return NAS_FEED_STALE;
    return NAS_FEED_LIVE;
}

static nas_level_t worse(nas_level_t a, nas_level_t b) { return a > b ? a : b; }

nas_level_t nas_pool_level(const nas_pool_t *p) {
    if (!p) return NAS_OK;
    nas_level_t l = NAS_OK;
    /* The schema: anything but ONLINE is a warning. DEGRADED still serves
     * data; every other state does not, so those are red. */
    if (strcmp(p->health, "ONLINE") != 0)
        l = strcmp(p->health, "DEGRADED") == 0 ? NAS_WARN : NAS_CRIT;
    if (p->errors > 0) l = worse(l, NAS_WARN);
    if (p->has_scrub && p->scrub_errors > 0) l = worse(l, NAS_WARN);
    return l;
}

nas_level_t nas_disk_level(const nas_disk_t *d) {
    if (!d) return NAS_OK;
    nas_level_t l = NAS_OK;
    /* A spun-down disk is normal (WI #3956: render it neutrally). */
    if (strcmp(d->power_state, "standby") == 0) l = NAS_NEUTRAL;
    /* Reallocated sectors are worth showing; with no history to see a rise,
     * a constant count is not a fault. */
    if (d->reallocated_sectors > 0) l = worse(l, NAS_NEUTRAL);
    if (d->pending_sectors > 0 || d->media_errors > 0) l = worse(l, NAS_WARN);
    if (d->percentage_used >= 100) l = worse(l, NAS_CRIT);
    else if (d->percentage_used >= 80) l = worse(l, NAS_WARN);
    if (d->has_temp) {
        bool nvme = strcmp(d->kind, "nvme") == 0;
        double warn = nvme ? 70.0 : 50.0, crit = nvme ? 80.0 : 60.0;
        if (d->temp_c >= crit) l = worse(l, NAS_CRIT);
        else if (d->temp_c >= warn) l = worse(l, NAS_WARN);
    }
    if (d->smart_passed == 0) l = NAS_CRIT;
    return l;
}

nas_level_t nas_overall_level(const nas_record_t *r) {
    if (!r) return NAS_OK;
    nas_level_t l = NAS_OK;
    if (r->source_stale || !r->has_collected) l = NAS_WARN;
    for (int i = 0; i < r->npools; i++) l = worse(l, nas_pool_level(&r->pools[i]));
    for (int i = 0; i < r->ndisks; i++) {
        nas_level_t d = nas_disk_level(&r->disks[i]);
        if (d != NAS_NEUTRAL) l = worse(l, d);
    }
    return l;
}

void nas_format_age(double s, char *buf, size_t n) {
    if (!buf || n == 0) return;
    if (s < 0) s = 0;
    if (s < 60) snprintf(buf, n, "%d s", (int)s);
    else if (s < 3600) snprintf(buf, n, "%d m", (int)(s / 60));
    else if (s < 86400) snprintf(buf, n, "%d h", (int)(s / 3600));
    else snprintf(buf, n, "%d d", (int)(s / 86400));
}

void nas_format_bytes(int64_t b, char *buf, size_t n) {
    if (!buf || n == 0) return;
    if (b < 0) {
        snprintf(buf, n, "?");
        return;
    }
    const double tb = 1e12, gb = 1e9;
    if ((double)b >= tb) snprintf(buf, n, "%.1f TB", (double)b / tb);
    else snprintf(buf, n, "%.0f GB", (double)b / gb);
}
