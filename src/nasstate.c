/* nasstate.c — kdash:nas:{host} v1 model for the NAS big panel (WI #3956).
 * See nasstate.h for the freshness rules. */
#include "nasstate.h"

#include <cjson/cJSON.h>
#include <stdarg.h>
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

/* ---- Disk use and order (sprint 033) ---- */

#include "nas_disk_uses.h"

#define NAS_SLOT_UNLISTED 1000

static int disk_slot(const nas_disk_t *d, nas_use_t *use) {
    for (size_t i = 0; i < sizeof(NAS_DISK_USES) / sizeof(NAS_DISK_USES[0]); i++)
        if (strcmp(d->model, NAS_DISK_USES[i].model) == 0) {
            if (use) *use = NAS_DISK_USES[i].use;
            return NAS_DISK_USES[i].slot;
        }
    if (use) *use = NAS_USE_NONE;
    return NAS_SLOT_UNLISTED;
}

nas_use_t nas_disk_use(const nas_disk_t *d) {
    nas_use_t u = NAS_USE_NONE;
    if (d) disk_slot(d, &u);
    return u;
}

const char *nas_use_label(nas_use_t u) {
    switch (u) {
    case NAS_USE_ROOT: return "root";
    case NAS_USE_POOL: return "nas pool";
    case NAS_USE_CACHE: return "cache";
    case NAS_USE_COLD: return "cold";
    case NAS_USE_NONE:
    default: return "";
    }
}

const char *nas_short_device(const char *dev) {
    const char *s = strrchr(dev, '/');
    return s ? s + 1 : dev;
}

/* Slot first, then the device name: the tie-break for disks that share a
 * model, and the whole order among unlisted ones. */
static bool disk_before(const nas_disk_t *a, const nas_disk_t *b) {
    int sa = disk_slot(a, NULL), sb = disk_slot(b, NULL);
    if (sa != sb) return sa < sb;
    return strcmp(nas_short_device(a->device), nas_short_device(b->device)) < 0;
}

int nas_disk_order(const nas_record_t *r, int *idx, int max) {
    if (!r || !idx || max <= 0) return 0;
    int all[NAS_DISKS_MAX];
    int n = r->ndisks < NAS_DISKS_MAX ? r->ndisks : NAS_DISKS_MAX;
    for (int i = 0; i < n; i++) {
        int j = i;
        while (j > 0 && disk_before(&r->disks[i], &r->disks[all[j - 1]])) {
            all[j] = all[j - 1];
            j--;
        }
        all[j] = i;
    }
    if (n > max) n = max;
    memcpy(idx, all, (size_t)n * sizeof(int));
    return n;
}

void nas_disk_reason(const nas_disk_t *d, char *buf, size_t n) {
    if (!buf || n == 0) return;
    buf[0] = '\0';
    if (!d || nas_disk_level(d) < NAS_WARN) return;
    bool nvme = strcmp(d->kind, "nvme") == 0;
    double t_warn = nvme ? 70.0 : 50.0, t_crit = nvme ? 80.0 : 60.0;
    /* Worst first, matching nas_disk_level's thresholds. */
    if (d->smart_passed == 0) snprintf(buf, n, "SMART FAILED");
    else if (d->percentage_used >= 100) snprintf(buf, n, "WEAR %lld%%", (long long)d->percentage_used);
    else if (d->has_temp && d->temp_c >= t_crit) snprintf(buf, n, "HOT %.0f\xc2\xb0", d->temp_c);
    else if (d->pending_sectors > 0) snprintf(buf, n, "PENDING %lld", (long long)d->pending_sectors);
    else if (d->media_errors > 0) snprintf(buf, n, "MEDIA ERR %lld", (long long)d->media_errors);
    else if (d->percentage_used >= 80) snprintf(buf, n, "WEAR %lld%%", (long long)d->percentage_used);
    else if (d->has_temp && d->temp_c >= t_warn) snprintf(buf, n, "HOT %.0f\xc2\xb0", d->temp_c);
}

static void append(char *buf, size_t n, const char *fmt, ...) __attribute__((format(printf, 3, 4)));
static void append(char *buf, size_t n, const char *fmt, ...) {
    size_t used = strlen(buf);
    if (used + 1 >= n) return;
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf + used, n - used, fmt, ap);
    va_end(ap);
}

void nas_headline(const nas_record_t *r, nas_feed_t feed, double now, char *head, size_t hn,
                  char *sub, size_t sn, nas_level_t *level) {
    if (!head || !sub || hn == 0 || sn == 0) return;
    head[0] = sub[0] = '\0';
    nas_level_t lvl = r ? nas_overall_level(r) : NAS_OK;
    char age[16];

    if (!r || feed == NAS_FEED_NONE) {
        snprintf(head, hn, "NO FEED");
        snprintf(sub, sn, "nothing published yet");
        if (level) *level = NAS_NEUTRAL;
        return;
    }
    if (feed == NAS_FEED_STALE) {
        nas_format_age(now - r->ts, age, sizeof(age));
        snprintf(head, hn, "STALE \xc2\xb7 %s", age);
        snprintf(sub, sn, "last values shown \xc2\xb7 no data for %s", age);
        if (level) *level = lvl;
        return;
    }

    /* The headline: the first thing in trouble, in precedence order. */
    const nas_pool_t *bad_pool = NULL;
    for (int i = 0; i < r->npools && !bad_pool; i++)
        if (nas_pool_level(&r->pools[i]) > NAS_OK) bad_pool = &r->pools[i];
    int order[NAS_DISKS_MAX];
    int nd = nas_disk_order(r, order, NAS_DISKS_MAX);
    const nas_disk_t *bad_disk = NULL;
    int nattn = 0;
    for (int i = 0; i < nd; i++) {
        const nas_disk_t *d = &r->disks[order[i]];
        nas_level_t dl = nas_disk_level(d);
        if (dl < NAS_WARN) continue;
        nattn++;
        if (!bad_disk || dl > nas_disk_level(bad_disk)) bad_disk = d;
    }

    if (bad_pool) {
        if (strcmp(bad_pool->health, "ONLINE") != 0)
            snprintf(head, hn, "%s %s", bad_pool->name, bad_pool->health);
        else if (bad_pool->errors > 0)
            snprintf(head, hn, "%s %lld ERRORS", bad_pool->name, (long long)bad_pool->errors);
        else
            snprintf(head, hn, "%s SCRUB ERRORS", bad_pool->name);
    } else if (bad_disk) {
        char why[32];
        nas_disk_reason(bad_disk, why, sizeof(why));
        snprintf(head, hn, "%s %s", nas_short_device(bad_disk->device), why);
    } else if (!r->has_collected) {
        snprintf(head, hn, "COLLECTOR DOWN");
    } else if (r->source_stale) {
        snprintf(head, hn, "SOURCE STALE");
    } else {
        snprintf(head, hn, "NAS OK");
    }

    /* The sub line: disks, then the first pool's scrub and error count. */
    if (nattn > 0)
        append(sub, sn, "%d disk%s need%s a look", nattn, nattn == 1 ? "" : "s",
               nattn == 1 ? "s" : "");
    else
        append(sub, sn, "%d disk%s", r->ndisks, r->ndisks == 1 ? "" : "s");
    if (r->npools > 0) {
        const nas_pool_t *p = &r->pools[0];
        if (p->has_scrub && strcmp(p->scrub_state, "scanning") == 0) {
            append(sub, sn, " \xc2\xb7 scrubbing");
        } else if (p->has_scrub && p->scrub_end_ts > 0) {
            nas_format_age(now - p->scrub_end_ts, age, sizeof(age));
            append(sub, sn, " \xc2\xb7 scrub %s ago", age);
        } else {
            append(sub, sn, " \xc2\xb7 no scrub");
        }
        if (nattn == 0) {
            long long errs = (p->errors > 0 ? p->errors : 0) +
                             (p->has_scrub && p->scrub_errors > 0 ? p->scrub_errors : 0);
            append(sub, sn, " \xc2\xb7 %lld error%s", errs, errs == 1 ? "" : "s");
        }
    }
    if (level) *level = lvl;
}
