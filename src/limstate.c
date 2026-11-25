/**
 * limstate.c — the Claude limits card's model (WI #4609). See the header.
 */
#include "limstate.h"

#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include <kdash/kdash_freshness.h>

static lim_record_t g_rec;
static bool g_have;
static kdash_codex_limits_t g_codex;
static bool g_have_codex;

/* The schema's host pattern, ^[A-Za-z0-9._-]{1,63}$. The field is display and
 * provenance only; anything else is not drawn rather than drawn wrong. */
static bool host_ok(const char *s) {
    size_t n = strlen(s);
    if (n == 0 || n >= LIM_HOST_MAX)
        return false;
    for (size_t i = 0; i < n; i++) {
        char c = s[i];
        if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
              c == '.' || c == '_' || c == '-'))
            return false;
    }
    return true;
}

bool lim_parse(const char *const *fields, const char *const *values, int n, lim_record_t *out) {
    memset(out, 0, sizeof(*out));
    if (!kdash_parse_claude_limits(fields, values, n, &out->l))
        return false;
    for (int i = 0; i < n; i++) {
        if (fields[i] && values[i] && strcmp(fields[i], "host") == 0 && host_ok(values[i])) {
            memcpy(out->host, values[i], strlen(values[i]) + 1);
            break;
        }
    }
    return true;
}

/* Carry the last scoped set into a record that arrived without one. */
static void carry_scoped(lim_record_t *dst, const lim_record_t *src) {
    kdash_claude_limits_t *d = &dst->l;
    const kdash_claude_limits_t *s = &src->l;
    d->scoped_valid = true;
    memcpy(d->scoped_model, s->scoped_model, sizeof(d->scoped_model));
    d->scoped_pct = s->scoped_pct;
    d->scoped_resets_at = s->scoped_resets_at;
    d->scoped_active = s->scoped_active;
    d->scoped_updated_at = s->scoped_updated_at;
    d->scoped_expected_refresh_s = s->scoped_expected_refresh_s;
}

int lim_observe(const char *const *fields, const char *const *values, int n) {
    if (!fields || !values || n <= 0)
        return 0;
    lim_record_t r;
    if (!lim_parse(fields, values, n, &r))
        return -1;
    if (g_have && !r.l.scoped_valid && g_rec.l.scoped_valid)
        carry_scoped(&r, &g_rec);
    if (g_have && memcmp(&r, &g_rec, sizeof(r)) == 0)
        return 0;
    g_rec = r;
    g_have = true;
    return 1;
}

bool lim_has_record(void) {
    return g_have;
}

const lim_record_t *lim_record(void) {
    return g_have ? &g_rec : NULL;
}

void lim_reset(void) {
    memset(&g_rec, 0, sizeof(g_rec));
    g_have = false;
    memset(&g_codex, 0, sizeof(g_codex));
    g_have_codex = false;
}

int lim_codex_observe(const char *const *fields, const char *const *values, int n) {
    if (!fields || !values || n <= 0)
        return 0;
    kdash_codex_limits_t c;
    if (!kdash_parse_codex_limits(fields, values, n, &c))
        return -1;
    if (g_have_codex && memcmp(&c, &g_codex, sizeof(c)) == 0)
        return 0;
    g_codex = c;
    g_have_codex = true;
    return 1;
}

const kdash_codex_limits_t *lim_codex(void) {
    return g_have_codex ? &g_codex : NULL;
}

bool lim_codex_stale(const kdash_codex_limits_t *c, double now) {
    return kdash_codex_limits_stale(c, (long long)now, KDASH_CADENCE_GRACE_S);
}

bool lim_has_any(void) {
    return g_have || g_have_codex;
}

bool lim_resets_agree(double a, double b) {
    return a > 0 && b > 0 && fabs(a - b) <= LIM_RESET_AGREE_S;
}

bool lim_headline_stale(const lim_record_t *r, double now) {
    return kdash_claude_limits_stale(&r->l, (long long)now, KDASH_CLAUDE_LIMITS_GRACE_S,
                                     KDASH_CLAUDE_LIMITS_STALE_S);
}

bool lim_scoped_stale(const lim_record_t *r, double now) {
    return kdash_claude_limits_scoped_stale(&r->l, (long long)now, KDASH_CLAUDE_LIMITS_GRACE_S,
                                            KDASH_CLAUDE_LIMITS_STALE_S);
}

/* ---- Derivations ---- */

int lim_pct(double pct) {
    if (!(pct > 0))
        return 0;
    if (pct >= 100)
        return 100;
    return (int)floor(pct + 0.5);
}

lim_band_t lim_band(int pct) {
    if (pct >= LIM_CRIT_PCT)
        return LIM_BAND_CRIT;
    if (pct >= LIM_WARN_PCT)
        return LIM_BAND_WARN;
    return LIM_BAND_OK;
}

double lim_pace(double resets_at, double window_s, double now) {
    if (resets_at <= 0 || window_s <= 0)
        return -1;
    double left = resets_at - now;
    if (left > window_s)
        return -1;
    if (left <= 0)
        return 1;
    return 1 - left / window_s;
}

double lim_gauge_deg(double frac) {
    if (frac < 0)
        frac = 0;
    else if (frac > 1)
        frac = 1;
    return LIM_GAUGE_START_DEG + LIM_GAUGE_SWEEP_DEG * frac;
}

/* ---- Formatting ---- */

void lim_fmt_reset(double resets_at, double now, char *buf, size_t n) {
    if (resets_at <= 0) {
        snprintf(buf, n, "reset unknown");
        return;
    }
    double left = resets_at - now;
    if (left <= 0) {
        snprintf(buf, n, "resets now");
        return;
    }
    if (left < 86400) {
        long mins = (long)ceil(left / 60);
        long h = mins / 60, m = mins % 60;
        if (h == 0)
            snprintf(buf, n, "resets in %ldm", m);
        else if (m == 0)
            snprintf(buf, n, "resets in %ldh", h);
        else
            snprintf(buf, n, "resets in %ldh %ldm", h, m);
        return;
    }
    time_t t = (time_t)resets_at;
    struct tm tm;
    char when[24];
    if (!localtime_r(&t, &tm) || strftime(when, sizeof(when), "%a %H:%M", &tm) == 0) {
        snprintf(buf, n, "reset unknown");
        return;
    }
    snprintf(buf, n, "resets %s", when);
}

void lim_fmt_dur(double secs, char *buf, size_t n) {
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

void lim_fmt_header(const lim_record_t *r, double now, bool stale, char *buf, size_t n) {
    char d[24]; /* a long's 20 digits and a unit: -O3 range analysis wants it */
    lim_fmt_dur(now - r->l.updated_at, d, sizeof(d));
    if (stale)
        snprintf(buf, n, "no data %s", d);
    else if (r->host[0])
        snprintf(buf, n, "%s \xC2\xB7 %s ago", r->host, d);
    else
        snprintf(buf, n, "%s ago", d);
}

void lim_fmt_scoped_ring_label(const lim_record_t *r, bool window, char *buf, size_t n) {
    char m[sizeof(r->l.scoped_model)];
    size_t i = 0;
    for (; i + 1 < sizeof(m) && r->l.scoped_model[i]; i++) {
        unsigned char c = (unsigned char)r->l.scoped_model[i];
        m[i] = c < 0x80 ? (char)toupper(c) : (char)c;
    }
    m[i] = '\0';
    if (window)
        snprintf(buf, n, "%s \xC2\xB7 7 DAY", m);
    else
        snprintf(buf, n, "%s", m);
}
