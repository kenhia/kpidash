/**
 * kkstate.c — the korg + klams card's model (WI #4613). See the header.
 */
#include "kkstate.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <kdash/kdash_freshness.h>

/* ---- korg ---- */

static kdash_korg_pulse_t g_korg;
static bool g_have_korg;
static char *g_korg_raw; /* the last value seen, so an unchanged poll is a strcmp */

int kk_observe_korg(const char *json) {
    if (!json)
        return 0;
    if (g_korg_raw && strcmp(json, g_korg_raw) == 0)
        return 0;
    free(g_korg_raw);
    g_korg_raw = strdup(json);
    kdash_korg_pulse_t p;
    if (!kdash_parse_korg_pulse(json, strlen(json), &p))
        return -1;
    if (g_have_korg && memcmp(&p, &g_korg, sizeof(p)) == 0)
        return 0;
    g_korg = p;
    g_have_korg = true;
    return 1;
}

const kdash_korg_pulse_t *kk_korg(void) {
    return g_have_korg ? &g_korg : NULL;
}

bool kk_korg_stale(double now) {
    return !g_have_korg ||
           kdash_cadence_stale(g_korg.ts, g_korg.expected_refresh_s, (long long)now,
                               KDASH_CADENCE_GRACE_S);
}

/* Days since 1970-01-01 for a `YYYY-MM-DD`, proleptic Gregorian (Howard
 * Hinnant's days_from_civil). No timezone: the date is korg's own. */
static bool day_number(const char *s, long *out) {
    int y, m, d;
    char tail;
    if (sscanf(s, "%4d-%2d-%2d%c", &y, &m, &d, &tail) != 3 || m < 1 || m > 12 || d < 1 ||
        d > 31)
        return false;
    y -= m <= 2;
    long era = (y >= 0 ? y : y - 399) / 400;
    long yoe = y - era * 400;
    long doy = (153L * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    long doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    *out = era * 146097 + doe - 719468;
    return true;
}

int kk_korg_spark(const kdash_korg_pulse_t *p, kk_point_t *out, int max) {
    if (!p || p->history_count < 1 || max < 1)
        return 0;
    long newest;
    if (!day_number(p->history[p->history_count - 1].date, &newest))
        return 0;
    const long first = newest - (KK_SPARK_DAYS - 1);
    int n = 0, top = 0;
    long days[KDASH_KORG_HISTORY_MAX];
    int vals[KDASH_KORG_HISTORY_MAX];
    for (int i = 0; i < p->history_count && n < max && n < KDASH_KORG_HISTORY_MAX; i++) {
        long d;
        if (!day_number(p->history[i].date, &d) || d < first || d > newest)
            continue;
        days[n] = d;
        vals[n] = p->history[i].open;
        if (vals[n] > top)
            top = vals[n];
        n++;
    }
    for (int i = 0; i < n; i++) {
        out[i].x = (double)(days[i] - first) / (KK_SPARK_DAYS - 1);
        out[i].y = top > 0 ? (double)vals[i] / top : 0;
    }
    return n;
}

/* ---- klams ---- */

static kdash_klams_status_t g_klams;
static bool g_have_klams;
static char *g_klams_raw;
static double g_klams_last_ts; /* the last DISTINCT record seen, adopted or not */
static kdash_klams_health_t g_seen_health;
static int g_health_run; /* consecutive records carrying g_seen_health */
static kdash_klams_health_t g_shown;
static int g_miss[KDASH_KLAMS_SUBSYSTEMS_MAX]; /* aligned with g_klams.subsystems */

static int miss_for(const char *name) {
    for (int i = 0; i < g_klams.subsystems_count; i++)
        if (strcmp(g_klams.subsystems[i].name, name) == 0)
            return g_miss[i];
    return 0;
}

int kk_observe_klams(const char *json) {
    if (!json)
        return 0;
    if (g_klams_raw && strcmp(json, g_klams_raw) == 0)
        return 0;
    free(g_klams_raw);
    g_klams_raw = strdup(json);
    kdash_klams_status_t s;
    if (!kdash_parse_klams_status(json, strlen(json), &s))
        return -1;

    const bool new_record = s.ts != g_klams_last_ts;
    if (new_record) {
        g_klams_last_ts = s.ts;
        if (g_health_run > 0 && s.health == g_seen_health)
            g_health_run++;
        else
            g_health_run = 1;
        g_seen_health = s.health;
    }
    const bool confirmed =
        !g_have_klams || s.health == KDASH_KLAMS_OK || g_health_run >= KK_CONFIRM;

    /* An unconfirmed `down` carries nothing but nulls: keep the last record. */
    if (s.health == KDASH_KLAMS_DOWN && !confirmed)
        return 0;

    int miss[KDASH_KLAMS_SUBSYSTEMS_MAX] = {0};
    for (int i = 0; i < s.subsystems_count; i++) {
        int prev = miss_for(s.subsystems[i].name);
        miss[i] = s.subsystems[i].ok ? 0 : (new_record ? prev + 1 : (prev > 0 ? prev : 1));
    }
    kdash_klams_health_t shown =
        confirmed ? s.health : (g_shown == KDASH_KLAMS_DOWN ? s.health : g_shown);

    if (g_have_klams && memcmp(&s, &g_klams, sizeof(s)) == 0 && shown == g_shown &&
        memcmp(miss, g_miss, sizeof(miss)) == 0)
        return 0;
    g_klams = s;
    memcpy(g_miss, miss, sizeof(miss));
    g_shown = shown;
    g_have_klams = true;
    return 1;
}

const kdash_klams_status_t *kk_klams(void) {
    return g_have_klams ? &g_klams : NULL;
}

bool kk_klams_stale(double now) {
    return !g_have_klams ||
           kdash_cadence_stale(g_klams.ts, g_klams.expected_refresh_s, (long long)now,
                               KDASH_CADENCE_GRACE_S);
}

kdash_klams_health_t kk_klams_health(void) {
    return g_shown;
}

bool kk_subsystem_down(int i) {
    return g_have_klams && i >= 0 && i < g_klams.subsystems_count && g_miss[i] >= KK_CONFIRM;
}

static double clamp01(double v) {
    return v < 0 ? 0 : v > 1 ? 1 : v;
}

void kk_scan_view(const kdash_klams_scanner_t *s, double now, kk_scan_view_t *out) {
    char d[24];
    memset(out, 0, sizeof(*out));
    const double iv = s->expected_interval_s > 0 ? s->expected_interval_s : 1;
    if (s->last_ran_at > 0 &&
        kdash_cadence_stale(s->last_ran_at, iv, (long long)now, KDASH_CADENCE_GRACE_S)) {
        out->state = KK_SCAN_STALLED;
        kk_fmt_dur(now - s->last_ran_at, d, sizeof(d));
        snprintf(out->text, sizeof(out->text), "ran %s ago", d);
        return;
    }
    if (s->last_ran_at > 0) {
        out->state = KK_SCAN_OK;
        out->frac = clamp01(1 - (now - s->last_ran_at) / iv);
    } else {
        out->state = KK_SCAN_NEUTRAL;
        out->frac = s->last_wrote_at > 0 ? clamp01(1 - (now - s->last_wrote_at) / iv) : 0;
    }
    if (s->last_wrote_at > 0) {
        kk_fmt_dur(now - s->last_wrote_at, d, sizeof(d));
        snprintf(out->text, sizeof(out->text), "wrote %s ago", d);
    } else {
        snprintf(out->text, sizeof(out->text), "never wrote");
    }
}

/* ---- formatting ---- */

void kk_fmt_dur(double secs, char *buf, size_t n) {
    long s = secs > 0 ? (long)secs : 0;
    if (s < 60)
        snprintf(buf, n, "%lds", s);
    else if (s < 3600)
        snprintf(buf, n, "%ldm", s / 60);
    else if (s < 86400)
        snprintf(buf, n, "%ldh", s / 3600);
    else
        snprintf(buf, n, "%ldd", s / 86400);
}

void kk_fmt_count(long long v, char *buf, size_t n) {
    if (n == 0)
        return;
    if (v < 0) {
        buf[0] = '\0';
        return;
    }
    char digits[24];
    int len = snprintf(digits, sizeof(digits), "%lld", v);
    size_t o = 0;
    for (int i = 0; i < len && o + 1 < n; i++) {
        if (i > 0 && (len - i) % 3 == 0 && o + 2 < n)
            buf[o++] = ',';
        buf[o++] = digits[i];
    }
    buf[o] = '\0';
}

void kk_fmt_nodata(double ts, double now, char *buf, size_t n) {
    char d[24];
    kk_fmt_dur(now - ts, d, sizeof(d));
    snprintf(buf, n, "no data %s", d);
}

void kk_reset(void) {
    free(g_korg_raw);
    free(g_klams_raw);
    g_korg_raw = g_klams_raw = NULL;
    memset(&g_korg, 0, sizeof(g_korg));
    memset(&g_klams, 0, sizeof(g_klams));
    memset(g_miss, 0, sizeof(g_miss));
    g_have_korg = g_have_klams = false;
    g_klams_last_ts = 0;
    g_seen_health = g_shown = KDASH_KLAMS_OK;
    g_health_run = 0;
}
