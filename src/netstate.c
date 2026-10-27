/* netstate.c — the network-health card's model (WI #4062). See netstate.h. */
#include "netstate.h"

#include <cjson/cJSON.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static net_host_t g_hosts[NET_HOSTS_MAX];
static int g_nhosts = 0;

/* ---- Parse ---- */

/* A nullable non-negative integer: -1 for null. Returns false when the field
 * is absent or the wrong type — present-and-null is a statement, absence is
 * a producer that forgot the field (CD-27 ruling 4). */
static bool get_nullable_long(const cJSON *root, const char *k, long *out) {
    const cJSON *v = cJSON_GetObjectItemCaseSensitive(root, k);
    if (cJSON_IsNull(v)) {
        *out = -1;
        return true;
    }
    if (!cJSON_IsNumber(v) || v->valuedouble < 0)
        return false;
    *out = (long)v->valuedouble;
    return true;
}

static bool get_nullable_double(const cJSON *obj, const char *k, double *out) {
    const cJSON *v = cJSON_GetObjectItemCaseSensitive(obj, k);
    if (cJSON_IsNull(v)) {
        *out = -1.0;
        return true;
    }
    if (!cJSON_IsNumber(v) || v->valuedouble < 0)
        return false;
    *out = v->valuedouble;
    return true;
}

static bool get_bool(const cJSON *obj, const char *k, bool *out) {
    const cJSON *v = cJSON_GetObjectItemCaseSensitive(obj, k);
    if (!cJSON_IsBool(v))
        return false;
    *out = cJSON_IsTrue(v);
    return true;
}

int net_parse_json(const char *json, net_record_t *out) {
    if (!json || !out)
        return -1;
    cJSON *root = cJSON_Parse(json);
    if (!root)
        return -1;
    net_record_t r;
    memset(&r, 0, sizeof(r));
    int rc = -1;

    const cJSON *leg = cJSON_GetObjectItemCaseSensitive(root, "leg");
    if (!cJSON_IsString(leg) || !leg->valuestring)
        goto done;
    if (strcmp(leg->valuestring, "10g") == 0)
        r.leg = NET_LEG_10G;
    else if (strcmp(leg->valuestring, "5g") == 0)
        r.leg = NET_LEG_5G;
    else if (strcmp(leg->valuestring, "1g") == 0)
        r.leg = NET_LEG_1G;
    else if (strcmp(leg->valuestring, "down") == 0)
        r.leg = NET_LEG_DOWN;
    else
        goto done; /* closed and lowercase: an unknown value distrusts the record */

    long speed;
    if (!get_nullable_long(root, "speed_mbps", &speed))
        goto done;
    r.speed_mbps = (int)speed;
    if (!get_bool(root, "bond", &r.bond))
        goto done;
    if (!get_nullable_long(root, "lfc", &r.lfc) ||
        !get_nullable_long(root, "lfc_delta_24h", &r.lfc_delta_24h) ||
        !get_nullable_long(root, "rx_errors", &r.rx_errors) ||
        !get_nullable_long(root, "rx_errors_delta_24h", &r.rx_errors_delta_24h))
        goto done;

    const cJSON *reach = cJSON_GetObjectItemCaseSensitive(root, "reach");
    if (!cJSON_IsObject(reach) || !get_bool(reach, "udm", &r.reach_udm) ||
        !get_bool(reach, "peers", &r.reach_peers) || !get_bool(reach, "wan", &r.reach_wan))
        goto done;
    const cJSON *rtt = cJSON_GetObjectItemCaseSensitive(root, "rtt_ms");
    if (!cJSON_IsObject(rtt) || !get_nullable_double(rtt, "udm", &r.rtt_udm) ||
        !get_nullable_double(rtt, "wan", &r.rtt_wan))
        goto done;

    const cJSON *bps = cJSON_GetObjectItemCaseSensitive(root, "bytes_per_s");
    if (!cJSON_IsNumber(bps) || bps->valuedouble < 0)
        goto done;
    r.bytes_per_s = bps->valuedouble;

    const cJSON *drops = cJSON_GetObjectItemCaseSensitive(root, "drops");
    if (!cJSON_IsArray(drops))
        goto done;
    const cJSON *d;
    cJSON_ArrayForEach(d, drops) {
        /* Unix seconds, like ts: a millisecond stamp is a validation failure. */
        if (!cJSON_IsNumber(d) || d->valuedouble <= 0 || d->valuedouble > 1e11)
            goto done;
        if (r.ndrops < NET_DROPS_MAX)
            r.drops[r.ndrops++] = d->valuedouble;
    }

    /* Seconds, bounded like every new family's ts: a millisecond stamp would
     * read as permanently fresh. */
    const cJSON *ts = cJSON_GetObjectItemCaseSensitive(root, "ts");
    if (!cJSON_IsNumber(ts) || ts->valuedouble <= 0 || ts->valuedouble > 1e11)
        goto done;
    r.ts = ts->valuedouble;

    const cJSON *nic = cJSON_GetObjectItemCaseSensitive(root, "nic");
    if (cJSON_IsString(nic) && nic->valuestring) {
        size_t len = strnlen(nic->valuestring, sizeof(r.nic) - 1);
        memcpy(r.nic, nic->valuestring, len);
        r.nic[len] = '\0';
    }

    *out = r;
    rc = 0;
done:
    cJSON_Delete(root);
    return rc;
}

/* ---- Registry ---- */

void net_set_hosts(const char hosts[][HOSTNAME_LEN], int n) {
    memset(g_hosts, 0, sizeof(g_hosts));
    g_nhosts = 0;
    for (int i = 0; i < n && g_nhosts < NET_HOSTS_MAX; i++) {
        if (!hosts[i][0])
            continue;
        size_t len = strnlen(hosts[i], HOSTNAME_LEN - 1);
        memcpy(g_hosts[g_nhosts].host, hosts[i], len);
        g_nhosts++;
    }
}

int net_host_count(void) { return g_nhosts; }

const net_host_t *net_host_at(int i) {
    return (i >= 0 && i < g_nhosts) ? &g_hosts[i] : NULL;
}

static void hist_push(net_host_t *h, float v) {
    if (h->hist_n == NET_HIST_N) {
        memmove(h->hist, h->hist + 1, sizeof(h->hist[0]) * (NET_HIST_N - 1));
        h->hist_n--;
    }
    h->hist[h->hist_n++] = v;
}

void net_observe(const char *host, const char *json, double now) {
    net_host_t *h = NULL;
    for (int i = 0; i < g_nhosts; i++) {
        if (strncmp(g_hosts[i].host, host, HOSTNAME_LEN) == 0) {
            h = &g_hosts[i];
            break;
        }
    }
    if (!h)
        return;

    net_record_t r;
    if (!json || net_parse_json(json, &r) != 0) {
        if (h->state == NET_COL_LIVE)
            h->state = NET_COL_STALE;
        return; /* history freezes while stale: there is nothing to add */
    }
    h->rec = r;
    h->state = NET_COL_LIVE;

    /* Per-minute means: 60 samples are the hour the card's label promises. */
    if (h->acc_n == 0 && h->minute_start <= 0)
        h->minute_start = now;
    h->acc_sum += r.bytes_per_s * 8.0 / 1e6;
    h->acc_n++;
    if (now - h->minute_start >= 60.0) {
        hist_push(h, (float)(h->acc_sum / h->acc_n));
        h->acc_sum = 0;
        h->acc_n = 0;
        h->minute_start = now;
    }
}

/* ---- Derivation ---- */

bool net_is_flapping(const net_host_t *h) {
    if (h->state == NET_COL_NOFEED || h->rec.leg == NET_LEG_1G || h->rec.leg == NET_LEG_DOWN)
        return false;
    return h->rec.ndrops >= 2 || h->rec.lfc_delta_24h >= 2;
}

net_attn_t net_column_attn(const net_host_t *h) {
    if (h->state == NET_COL_NOFEED)
        return NET_ATTN_NONE;
    if (h->state == NET_COL_STALE)
        return NET_ATTN_CRIT;
    const net_record_t *r = &h->rec;
    if (r->leg == NET_LEG_DOWN || !r->reach_udm || !r->reach_peers || !r->reach_wan)
        return NET_ATTN_CRIT;
    if (r->leg == NET_LEG_1G && r->bond)
        return NET_ATTN_WARN;
    if (r->lfc_delta_24h > 0 || r->ndrops > 0 || r->rx_errors_delta_24h > 0)
        return NET_ATTN_WARN;
    return NET_ATTN_NONE;
}

const char *net_leg_text(net_leg_t leg) {
    switch (leg) {
    case NET_LEG_10G:
        return "10G";
    case NET_LEG_5G:
        return "5G";
    case NET_LEG_1G:
        return "1G";
    case NET_LEG_DOWN:
    default:
        return "DOWN";
    }
}

static long age_s(const net_host_t *h, double now) {
    double a = now - h->rec.ts;
    return a < 0 ? 0 : (long)(a + 0.5);
}

void net_subtitle(const net_host_t *h, double now, char *buf, size_t n, bool *bold) {
    const net_record_t *r = &h->rec;
    *bold = true;
    if (h->state == NET_COL_NOFEED)
        snprintf(buf, n, "NO FEED");
    else if (h->state == NET_COL_STALE)
        snprintf(buf, n, "STALE · %ld s", age_s(h, now));
    else if (r->leg == NET_LEG_DOWN)
        snprintf(buf, n, "NO LINK");
    else if (net_is_flapping(h)) {
        int d = r->ndrops > 0 ? r->ndrops : (int)r->lfc_delta_24h;
        snprintf(buf, n, "FLAPPING · %d drop%s", d, d == 1 ? "" : "s");
    } else if (r->leg == NET_LEG_1G && r->bond)
        snprintf(buf, n, "10G down · on 1G backup");
    else if (!r->reach_udm)
        snprintf(buf, n, "no path to UDM");
    else if (!r->reach_wan)
        snprintf(buf, n, "no path to Internet");
    else if (!r->reach_peers)
        snprintf(buf, n, "no fabric peers");
    else {
        *bold = false;
        snprintf(buf, n, "%s", r->bond ? "bond primary" : "single NIC");
    }
}

void net_rx_text(const net_host_t *h, char *buf, size_t n) {
    if (h->state == NET_COL_NOFEED || h->rec.rx_errors_delta_24h < 0)
        snprintf(buf, n, "rx err —");
    else
        snprintf(buf, n, "rx err %ld", h->rec.rx_errors_delta_24h);
}

static int cmp_double(const void *a, const void *b) {
    double x = *(const double *)a, y = *(const double *)b;
    return (x > y) - (x < y);
}

static double median(double *v, int n) {
    if (n <= 0)
        return -1.0;
    qsort(v, (size_t)n, sizeof(v[0]), cmp_double);
    return (n % 2) ? v[n / 2] : (v[n / 2 - 1] + v[n / 2]) / 2.0;
}

/* Severity rank of one column's problem, for picking the headline. */
static int problem_rank(const net_host_t *h) {
    switch (net_column_attn(h)) {
    case NET_ATTN_CRIT:
        return h->state == NET_COL_STALE ? 2 : 3; /* stale reads as a warning headline */
    case NET_ATTN_WARN:
        return 1;
    default:
        return 0;
    }
}

static void host_headline(const net_host_t *h, double now, net_summary_t *s) {
    const net_record_t *r = &h->rec;
    if (h->state == NET_COL_STALE) {
        s->level = NET_LEVEL_WARN;
        snprintf(s->verdict, sizeof(s->verdict), "%s STALE", h->host);
        snprintf(s->why, sizeof(s->why), "no feed for %ld s", age_s(h, now));
    } else if (r->leg == NET_LEG_DOWN) {
        s->level = NET_LEVEL_CRIT;
        snprintf(s->verdict, sizeof(s->verdict), "%s DOWN", h->host);
        snprintf(s->why, sizeof(s->why), "%s", r->bond ? "no active leg on the bond" : "no carrier");
    } else if (!r->reach_udm || !r->reach_wan || !r->reach_peers) {
        s->level = NET_LEVEL_CRIT;
        snprintf(s->verdict, sizeof(s->verdict), "%s NO PATH", h->host);
        snprintf(s->why, sizeof(s->why), "%s",
                 !r->reach_udm   ? "UDM unreachable from this host"
                 : !r->reach_wan ? "Internet unreachable from this host"
                                 : "no fabric peer answers");
    } else if (net_is_flapping(h)) {
        s->level = NET_LEVEL_WARN;
        int d = r->ndrops > 0 ? r->ndrops : (int)r->lfc_delta_24h;
        snprintf(s->verdict, sizeof(s->verdict), "%s FLAPPING", h->host);
        if (r->rx_errors_delta_24h > 0)
            snprintf(s->why, sizeof(s->why), "%d link drops in %s · rx errors +%ld · still on %s",
                     d, r->ndrops > 0 ? "60 min" : "24 h", r->rx_errors_delta_24h,
                     net_leg_text(r->leg));
        else
            snprintf(s->why, sizeof(s->why), "%d link drops in %s · still on %s", d,
                     r->ndrops > 0 ? "60 min" : "24 h", net_leg_text(r->leg));
    } else if (r->leg == NET_LEG_1G && r->bond) {
        s->level = NET_LEVEL_WARN;
        snprintf(s->verdict, sizeof(s->verdict), "%s ON 1G", h->host);
        snprintf(s->why, sizeof(s->why), "%s 10G link down · bond on 1G backup", h->host);
    } else if (r->lfc_delta_24h > 0 || r->ndrops > 0) {
        s->level = NET_LEVEL_WARN;
        snprintf(s->verdict, sizeof(s->verdict), "%s LINK DROP", h->host);
        snprintf(s->why, sizeof(s->why), "1 link drop in %s · back on %s",
                 r->ndrops > 0 ? "60 min" : "24 h", net_leg_text(r->leg));
    } else {
        s->level = NET_LEVEL_WARN;
        snprintf(s->verdict, sizeof(s->verdict), "%s RX ERRORS", h->host);
        snprintf(s->why, sizeof(s->why), "rx errors +%ld in 24 h on %s", r->rx_errors_delta_24h,
                 net_leg_text(r->leg));
    }
}

__attribute__((format(printf, 3, 4))) static void append(char *buf, size_t n, const char *fmt,
                                                         ...) {
    size_t len = strnlen(buf, n);
    if (len + 1 >= n)
        return;
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf + len, n - len, fmt, ap);
    va_end(ap);
}

void net_derive(double now, net_summary_t *s) {
    memset(s, 0, sizeof(*s));
    double udm[NET_HOSTS_MAX], wan[NET_HOSTS_MAX];
    int nudm = 0, nwan = 0, nlive = 0, nstale = 0, npeers = 0;
    int nbond = 0, nbond10 = 0;
    for (int i = 0; i < g_nhosts; i++) {
        const net_host_t *h = &g_hosts[i];
        if (h->state == NET_COL_STALE)
            nstale++;
        if (h->state != NET_COL_LIVE)
            continue;
        nlive++;
        const net_record_t *r = &h->rec;
        if (r->reach_udm) {
            s->udm_up = true;
            if (r->rtt_udm >= 0)
                udm[nudm++] = r->rtt_udm;
        }
        if (r->reach_wan) {
            s->wan_up = true;
            if (r->rtt_wan >= 0)
                wan[nwan++] = r->rtt_wan;
        }
        if (r->reach_peers)
            npeers++;
        if (r->bond) {
            nbond++;
            if (r->leg == NET_LEG_10G)
                nbond10++;
        }
    }
    s->udm_ms = median(udm, nudm);
    s->wan_ms = median(wan, nwan);
    s->uplink_up = s->udm_up || nlive == 0;

    if (nlive == 0 && nstale == 0) {
        s->level = NET_LEVEL_NONE;
        snprintf(s->verdict, sizeof(s->verdict), "NO FEED");
        snprintf(s->why, sizeof(s->why), "no host is publishing kdash:net");
        s->uplink_up = false;
        return;
    }

    /* Fabric-wide states first: they explain every column at once. */
    if (nlive > 0 && !s->udm_up && npeers > 0) {
        s->level = NET_LEVEL_CRIT;
        s->uplink_up = false;
        snprintf(s->verdict, sizeof(s->verdict), "UPLINK LOST");
        snprintf(s->why, sizeof(s->why), "peers reachable · no host reaches UDM");
        return;
    }
    if (nlive > 0 && s->udm_up && !s->wan_up) {
        s->level = NET_LEVEL_CRIT;
        snprintf(s->verdict, sizeof(s->verdict), "WAN DOWN");
        snprintf(s->why, sizeof(s->why), "UDM reachable · Internet unreachable");
        return;
    }

    /* Otherwise the worst column is the headline. */
    const net_host_t *worst = NULL;
    int worst_rank = 0, nproblem = 0;
    for (int i = 0; i < g_nhosts; i++) {
        int rk = problem_rank(&g_hosts[i]);
        if (rk > 0)
            nproblem++;
        if (rk > worst_rank) {
            worst_rank = rk;
            worst = &g_hosts[i];
        }
    }
    if (worst) {
        host_headline(worst, now, s);
        if (nproblem > 1)
            append(s->why, sizeof(s->why), " · +%d more", nproblem - 1);
        return;
    }

    s->level = NET_LEVEL_OK;
    if (nbond > 0 && nbond10 == nbond)
        snprintf(s->verdict, sizeof(s->verdict), "ALL 10G");
    else
        snprintf(s->verdict, sizeof(s->verdict), "ALL UP");
    if (nbond > 0)
        snprintf(s->why, sizeof(s->why), "%d bond%s on 10G", nbond10, nbond10 == 1 ? "" : "s");
    for (int i = 0; i < g_nhosts; i++) {
        const net_host_t *h = &g_hosts[i];
        if (h->state == NET_COL_LIVE && !h->rec.bond)
            append(s->why, sizeof(s->why), "%s%s %s", s->why[0] ? " · " : "", h->host,
                   net_leg_text(h->rec.leg));
    }
    if (s->wan_ms >= 0)
        append(s->why, sizeof(s->why), "%sWAN %.0f ms", s->why[0] ? " · " : "", s->wan_ms);
    for (int i = 0; i < g_nhosts; i++)
        if (g_hosts[i].state == NET_COL_NOFEED)
            append(s->why, sizeof(s->why), " · %s no feed", g_hosts[i].host);
}
