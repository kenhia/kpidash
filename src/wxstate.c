/* wxstate.c — kdash:weather:home v1 model for the weather filler (WI #4576).
 * See wxstate.h for the freshness and trust rules. */
#include "wxstate.h"

#include <cjson/cJSON.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

/* The last accepted payload's text, so an unchanged GET (every poll for ten
 * minutes at a time) costs one strcmp rather than a parse. */
#define WX_RAW_MAX 4096

static wx_record_t g_rec;
static bool g_has_rec = false;
static char g_raw[WX_RAW_MAX];

void wx_reset(void) {
    memset(&g_rec, 0, sizeof(g_rec));
    g_has_rec = false;
    g_raw[0] = '\0';
}

/* ---- Parsing: strict, because the contract's counterexamples are the
 * rules. Every helper returns false when the record must be distrusted. ---- */

static const char *const COND_NAMES[] = {
    NULL, "clear", "partly", "cloudy", "fog", "drizzle", "rain", "sleet", "snow", "thunder",
};

/* Present, and either null or one of the closed set. */
static bool get_cond(const cJSON *o, const char *key, wx_cond_t *out) {
    const cJSON *c = cJSON_GetObjectItemCaseSensitive(o, key);
    if (!c) return false;
    if (cJSON_IsNull(c)) {
        *out = WX_COND_NONE;
        return true;
    }
    if (!cJSON_IsString(c) || !c->valuestring) return false;
    for (int i = 1; i <= WX_COND_THUNDER; i++) {
        if (strcmp(c->valuestring, COND_NAMES[i]) == 0) {
            *out = (wx_cond_t)i;
            return true;
        }
    }
    return false; /* a provider's own text on the wire */
}

/* Present, and either null (*has = false) or a number in [lo, hi]; whole if
 * `integer`. */
static bool get_num(const cJSON *o, const char *key, double lo, double hi, bool integer,
                    bool *has, double *v) {
    const cJSON *c = cJSON_GetObjectItemCaseSensitive(o, key);
    if (!c) return false; /* absent is not null (CD-27) */
    if (cJSON_IsNull(c)) {
        *has = false;
        *v = 0;
        return true;
    }
    if (!cJSON_IsNumber(c)) return false;
    double d = c->valuedouble;
    if (d < lo || d > hi) return false;
    if (integer && d != floor(d)) return false;
    *has = true;
    *v = d;
    return true;
}

static bool get_int(const cJSON *o, const char *key, double lo, double hi, int *out, int null_as) {
    bool has;
    double v;
    if (!get_num(o, key, lo, hi, true, &has, &v)) return false;
    *out = has ? (int)v : null_as;
    return true;
}

/* A sun event: null, or whole unix seconds in (0, 1e11]. */
static bool get_sun(const cJSON *o, const char *key, int64_t *out) {
    bool has;
    double v;
    if (!get_num(o, key, 1, WX_TS_MAX, true, &has, &v)) return false;
    *out = has ? (int64_t)v : 0;
    return true;
}

static bool parse_now(const cJSON *o, wx_now_t *n) {
    if (!cJSON_IsObject(o)) return false;
    const cJSON *d = cJSON_GetObjectItemCaseSensitive(o, "is_day");
    if (!cJSON_IsBool(d)) return false;
    n->is_day = cJSON_IsTrue(d);
    return get_num(o, "temp_f", -150, 150, false, &n->has_temp, &n->temp_f) &&
           get_num(o, "feels_f", -150, 150, false, &n->has_feels, &n->feels_f) &&
           get_int(o, "humidity_pct", 0, 100, &n->humidity_pct, -1) &&
           get_cond(o, "cond", &n->cond);
}

static bool parse_sun(const cJSON *o, wx_sun_t *s) {
    return cJSON_IsObject(o) && get_sun(o, "first_light", &s->first_light) &&
           get_sun(o, "sunrise", &s->sunrise) && get_sun(o, "sunset", &s->sunset) &&
           get_sun(o, "last_light", &s->last_light);
}

static bool parse_day(const cJSON *o, wx_day_t *d) {
    if (!cJSON_IsObject(o)) return false;
    const cJSON *date = cJSON_GetObjectItemCaseSensitive(o, "date");
    if (!cJSON_IsString(date) || !date->valuestring || strlen(date->valuestring) > 10)
        return false;
    memcpy(d->date, date->valuestring, strlen(date->valuestring) + 1);
    bool has;
    double v;
    if (!get_num(o, "hi_f", -150, 150, true, &has, &v)) return false;
    d->has_hi = has;
    d->hi_f = (int)v;
    if (!get_num(o, "lo_f", -150, 150, true, &has, &v)) return false;
    d->has_lo = has;
    d->lo_f = (int)v;
    return get_cond(o, "cond", &d->cond) && get_int(o, "pop_pct", 0, 100, &d->pop_pct, -1);
}

int wx_parse(const char *json, wx_record_t *out) {
    if (!out) return -1;
    memset(out, 0, sizeof(*out));
    if (!json) return -1;
    cJSON *root = cJSON_Parse(json);
    if (!root) return -1;
    bool ok = cJSON_IsObject(root);
    const cJSON *ts = cJSON_GetObjectItemCaseSensitive(root, "ts");
    ok = ok && cJSON_IsNumber(ts) && ts->valuedouble > 0 && ts->valuedouble <= WX_TS_MAX;
    if (ok) out->ts = ts->valuedouble;
    const cJSON *src = cJSON_GetObjectItemCaseSensitive(root, "source");
    ok = ok && cJSON_IsString(src) && src->valuestring;
    if (ok) snprintf(out->source, sizeof(out->source), "%s", src->valuestring);
    ok = ok && parse_now(cJSON_GetObjectItemCaseSensitive(root, "now"), &out->now);
    ok = ok && parse_sun(cJSON_GetObjectItemCaseSensitive(root, "sun"), &out->sun);
    const cJSON *days = cJSON_GetObjectItemCaseSensitive(root, "days");
    ok = ok && cJSON_IsArray(days) && cJSON_GetArraySize(days) <= WX_DAYS_MAX;
    if (ok) {
        const cJSON *item;
        cJSON_ArrayForEach(item, days) {
            if (!parse_day(item, &out->days[out->ndays++])) {
                ok = false;
                break;
            }
        }
    }
    cJSON_Delete(root);
    if (!ok) {
        memset(out, 0, sizeof(*out));
        return -1;
    }
    return 0;
}

int wx_observe(const char *json) {
    if (!json) return 0; /* no TTL: absent keeps the last record, ageing */
    if (g_raw[0] && strcmp(json, g_raw) == 0) return 0;
    wx_record_t rec;
    /* Remember the text either way, so a distrusted record is reported
     * once rather than every second until kweather writes the next one. */
    snprintf(g_raw, sizeof(g_raw), "%s", json);
    if (wx_parse(json, &rec) != 0) return -1;
    g_rec = rec;
    g_has_rec = true;
    return 1;
}

bool wx_has_record(void) { return g_has_rec; }
const wx_record_t *wx_record(void) { return &g_rec; }

bool wx_is_stale(const wx_record_t *r, double now) {
    return !r || now - r->ts > WX_FRESH_S;
}

/* ---- Space ---- */

int wx_columns(int host_cols) {
    int n = WX_ROW_COLS - host_cols;
    if (n < 0) n = 0;
    return n > WX_MAX_COLS ? WX_MAX_COLS : n;
}

int wx_width(int n) { return n > 0 ? WX_PITCH * n - 14 : 0; }

int wx_left(int host_cols) {
    if (host_cols < 0) host_cols = 0;
    int x = WX_PITCH * host_cols;
    if (host_cols < 6) x += WX_PITCH * (6 - host_cols) / 2;
    return x;
}

int wx_days_shown(int n, int ndays) {
    static const int per[] = {0, 0, 3, 6, 8};
    int k = (n >= 0 && n <= WX_MAX_COLS) ? per[n] : 0;
    return ndays < k ? ndays : k;
}

/* ---- Derivations ---- */

bool wx_temp_scale(const wx_record_t *r, int k, int *tmin, int *tmax) {
    bool any = false;
    int lo = 0, hi = 0;
    for (int i = 0; i < k && i < r->ndays; i++) {
        const wx_day_t *d = &r->days[i];
        int v[2], nv = 0;
        if (d->has_hi) v[nv++] = d->hi_f;
        if (d->has_lo) v[nv++] = d->lo_f;
        for (int j = 0; j < nv; j++) {
            if (!any || v[j] < lo) lo = v[j];
            if (!any || v[j] > hi) hi = v[j];
            any = true;
        }
    }
    *tmin = lo;
    *tmax = hi;
    return any;
}

int wx_scale_y(int v, int tmin, int tmax, int y_top, int y_bot) {
    int span = tmax - tmin;
    if (span <= 0) return y_bot;
    return y_bot - (int)lround((double)(v - tmin) * (y_bot - y_top) / span);
}

double wx_day_frac(double ts) {
    time_t t = (time_t)ts;
    struct tm tm;
    if (!localtime_r(&t, &tm)) return 0;
    return (tm.tm_hour * 60 + tm.tm_min) / 1440.0;
}

/* ---- Formatting ---- */

#define DASH "\xE2\x80\x94" /* — */
#define DEG "\xC2\xB0"      /* ° */
#define MIDDOT "\xC2\xB7"   /* · */

void wx_fmt_deg(bool has, double v, char *buf, size_t n) {
    if (!has)
        snprintf(buf, n, DASH);
    else
        snprintf(buf, n, "%d" DEG, (int)floor(v + 0.5));
}

void wx_fmt_time(int64_t ts, char *buf, size_t n) {
    struct tm tm;
    time_t t = (time_t)ts;
    if (ts <= 0 || !localtime_r(&t, &tm)) {
        snprintf(buf, n, DASH);
        return;
    }
    int h = tm.tm_hour % 12;
    snprintf(buf, n, "%d:%02d %s", h ? h : 12, tm.tm_min, tm.tm_hour < 12 ? "AM" : "PM");
}

void wx_fmt_weekday(const wx_day_t *d, int i, char *buf, size_t n) {
    static const char *const WD[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
    if (i == 0) {
        snprintf(buf, n, "Today");
        return;
    }
    int y, m, dd;
    if (sscanf(d->date, "%4d-%2d-%2d", &y, &m, &dd) != 3 || m < 1 || m > 12 || dd < 1 ||
        dd > 31) {
        snprintf(buf, n, DASH);
        return;
    }
    /* Days from civil (Howard Hinnant), then 1970-01-01 was a Thursday. */
    y -= m <= 2;
    long era = (y >= 0 ? y : y - 399) / 400;
    long yoe = y - era * 400;
    long doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + dd - 1;
    long doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    long days = era * 146097 + doe - 719468;
    int wd = (int)(((days % 7) + 7 + 4) % 7);
    snprintf(buf, n, "%s", WD[wd]);
}

void wx_fmt_daylight(const wx_sun_t *s, char *buf, size_t n) {
    if (!s->sunrise || !s->sunset || s->sunset <= s->sunrise) {
        snprintf(buf, n, "no sunrise today");
        return;
    }
    long m = lround((s->sunset - s->sunrise) / 60.0);
    snprintf(buf, n, "%ld h %ld m of daylight", m / 60, m % 60);
}

const char *wx_cond_name(wx_cond_t c) {
    static const char *const NAMES[] = {
        "No current reading", "Clear", "Partly cloudy", "Cloudy", "Fog",
        "Drizzle",            "Rain",  "Sleet",         "Snow",   "Thunderstorm",
    };
    return (c >= WX_COND_NONE && c <= WX_COND_THUNDER) ? NAMES[c] : NAMES[0];
}

void wx_fmt_feels(const wx_now_t *now, char *buf, size_t n) {
    if (!now->has_temp) {
        snprintf(buf, n, "forecast only");
        return;
    }
    char f[16], h[16];
    wx_fmt_deg(now->has_feels, now->feels_f, f, sizeof(f));
    if (now->humidity_pct >= 0)
        snprintf(h, sizeof(h), "%d%%", now->humidity_pct);
    else
        snprintf(h, sizeof(h), DASH);
    snprintf(buf, n, "feels %s " MIDDOT " %s humidity", f, h);
}

void wx_fmt_age(const wx_record_t *r, double now, char *buf, size_t n) {
    double s = now - r->ts;
    if (s < 0) s = 0;
    long m = (long)(s / 60);
    char a[32];
    if (m < 60)
        snprintf(a, sizeof(a), "%ld m", m);
    else if (m < 48 * 60)
        snprintf(a, sizeof(a), "%ld h", m / 60);
    else
        snprintf(a, sizeof(a), "%ld d", m / 1440);
    if (wx_is_stale(r, now))
        snprintf(buf, n, "STALE " MIDDOT " %s", a);
    else if (m < 1)
        snprintf(buf, n, "just now");
    else
        snprintf(buf, n, "%s ago", a);
}
