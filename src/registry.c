#include "registry.h"

#include <pthread.h>
#include <stdio.h>
#include <string.h>

/* WI #2244: the shared apartment-temperature band classifier and its default
 * thresholds. kpidash links kdash_core only — the pure logic, no sockets. */
#include <kdash/kdash_freshness.h>
#include <kdash/kdash_payload.h>

/* ---- Global singleton ---- */
static client_info_t g_clients[MAX_CLIENTS];
static int g_count = 0;
static pthread_mutex_t g_mutex = PTHREAD_MUTEX_INITIALIZER;

/* WI #3012: hosts that have published at least once, EVER — seeded from the
 * admitted-hosts file at startup, appended to when a new host first
 * publishes, and never pruned (the same rule `kpidash:clients` itself
 * follows). This is what survives a restart, and so what keeps a host that
 * is down at boot showing a red card instead of no card. */
static char g_admitted[MAX_CLIENTS][HOSTNAME_LEN];
static int g_admitted_count = 0;
static bool g_admitted_dirty = false;

void registry_init(void) {
    pthread_mutex_lock(&g_mutex);
    memset(g_clients, 0, sizeof(g_clients));
    g_count = 0;
    memset(g_admitted, 0, sizeof(g_admitted));
    g_admitted_count = 0;
    g_admitted_dirty = false;
    pthread_mutex_unlock(&g_mutex);
}

void registry_lock(void) {
    pthread_mutex_lock(&g_mutex);
}

void registry_unlock(void) {
    pthread_mutex_unlock(&g_mutex);
}

/* Priority client list set once by registry_set_priority_clients() */
static char g_priority[MAX_CLIENTS][HOSTNAME_LEN];
static int g_priority_count = 0;

void registry_set_priority_clients(const char hosts[][HOSTNAME_LEN], int count) {
    pthread_mutex_lock(&g_mutex);
    g_priority_count = count < MAX_CLIENTS ? count : MAX_CLIENTS;
    for (int i = 0; i < g_priority_count; i++) {
        strncpy(g_priority[i], hosts[i], HOSTNAME_LEN - 1);
        g_priority[i][HOSTNAME_LEN - 1] = '\0';
    }
    pthread_mutex_unlock(&g_mutex);
}

static bool is_priority(const char *hostname) {
    for (int i = 0; i < g_priority_count; i++) {
        if (strncmp(g_priority[i], hostname, HOSTNAME_LEN) == 0)
            return true;
    }
    return false;
}

int registry_priority_index(const char *hostname) {
    for (int i = 0; i < g_priority_count; i++) {
        if (strncmp(g_priority[i], hostname, HOSTNAME_LEN) == 0)
            return i;
    }
    return -1;
}

client_info_t *registry_find(const char *hostname) {
    /* Must be called with lock held. */
    if (!hostname || !hostname[0])
        return NULL;

    for (int i = 0; i < g_count; i++) {
        if (strncmp(g_clients[i].hostname, hostname, HOSTNAME_LEN) == 0) {
            return &g_clients[i];
        }
    }
    return NULL;
}

/* Must be called with the lock held. */
static bool is_admitted(const char *hostname) {
    for (int i = 0; i < g_admitted_count; i++) {
        if (strncmp(g_admitted[i], hostname, HOSTNAME_LEN) == 0)
            return true;
    }
    return false;
}

/* Must be called with the lock held. Returns true if the set grew. */
static bool remember_admitted(const char *hostname) {
    if (is_admitted(hostname))
        return false;

    if (g_admitted_count >= MAX_CLIENTS) {
        /* The registry is bounded by the same number, so this only happens
         * on a fleet that has outgrown the panel. Say so once and carry on:
         * the host still gets its card this run, it just will not survive a
         * restart, which is strictly better than dropping it now. */
        fprintf(stderr, "registry: admitted set full (%d), '%s' will not persist\n", MAX_CLIENTS,
                hostname);
        return false;
    }

    strncpy(g_admitted[g_admitted_count], hostname, HOSTNAME_LEN - 1);
    g_admitted[g_admitted_count][HOSTNAME_LEN - 1] = '\0';
    g_admitted_count++;
    return true;
}

client_info_t *registry_admit(const char *hostname, bool has_client_data) {
    /* WI #2524: create on data, find otherwise. See registry.h for why the
     * second half is not a detail — it is what keeps an outage visible. */
    client_info_t *existing = registry_find(hostname);
    if (existing)
        return existing;

    if (!hostname || !hostname[0])
        return NULL;

    if (has_client_data) {
        /* First payload this run. Recording it is what makes the admission
         * outlive the process (WI #3012). */
        if (remember_admitted(hostname))
            g_admitted_dirty = true;
        return registry_find_or_create(hostname);
    }

    /* No data right now — but if this host was admitted in an earlier run,
     * it has published before and its silence is news. Give it a card so it
     * can go red. */
    if (is_admitted(hostname))
        return registry_find_or_create(hostname);

    return NULL;
}

int registry_seed_admitted(const char hosts[][HOSTNAME_LEN], int count) {
    if (!hosts || count <= 0)
        return 0;

    pthread_mutex_lock(&g_mutex);
    int before = g_admitted_count;
    for (int i = 0; i < count; i++) {
        if (!hosts[i][0])
            continue;
        remember_admitted(hosts[i]);
    }
    int accepted = g_admitted_count - before;
    /* Deliberately NOT marking dirty: this came off the disk, and writing it
     * straight back would be a disk write on every boot that changes
     * nothing. */
    pthread_mutex_unlock(&g_mutex);
    return accepted;
}

int registry_admitted_snapshot(char out[][HOSTNAME_LEN], int max) {
    if (!out || max <= 0)
        return 0;

    pthread_mutex_lock(&g_mutex);
    int n = g_admitted_count < max ? g_admitted_count : max;
    for (int i = 0; i < n; i++) {
        /* memcpy of the whole element, not strncpy: source and destination
         * are both exactly HOSTNAME_LEN, so this copies the terminator with
         * the name and cannot truncate. strncpy here makes GCC's
         * -Wstringop-truncation fire at -O2 — caught by `just check-release`,
         * which is the flow-sensitive class that only runs with the
         * optimiser (WI #1934). */
        memcpy(out[i], g_admitted[i], HOSTNAME_LEN);
    }
    pthread_mutex_unlock(&g_mutex);
    return n;
}

bool registry_admitted_take_dirty(void) {
    pthread_mutex_lock(&g_mutex);
    bool was = g_admitted_dirty;
    g_admitted_dirty = false;
    pthread_mutex_unlock(&g_mutex);
    return was;
}

client_info_t *registry_find_or_create(const char *hostname) {
    /* Search existing (must be called with lock held) */
    client_info_t *existing = registry_find(hostname);
    if (existing)
        return existing;
    if (!hostname || !hostname[0])
        return NULL;

    /* Allocate new slot if space available */
    if (g_count < MAX_CLIENTS) {
        client_info_t *c = &g_clients[g_count];
        memset(c, 0, sizeof(*c));
        strncpy(c->hostname, hostname, HOSTNAME_LEN - 1);
        g_count++;
        fprintf(stderr, "registry: new client '%s' (slot %d)\n", c->hostname, g_count);
        return c;
    }

    /* Registry full — evict the non-priority client with oldest last_seen_ts */
    int evict_idx = -1;
    double oldest_ts = 0.0;
    for (int i = 0; i < g_count; i++) {
        if (!is_priority(g_clients[i].hostname)) {
            if (evict_idx == -1 || g_clients[i].last_seen_ts < oldest_ts) {
                evict_idx = i;
                oldest_ts = g_clients[i].last_seen_ts;
            }
        }
    }

    if (evict_idx == -1) {
        /* All slots are priority clients; cannot make room */
        fprintf(stderr, "registry: max clients (%d) reached and all are priority\n", MAX_CLIENTS);
        return NULL;
    }

    fprintf(stderr, "registry: evicting '%s' (last_seen_ts=%.0f) to make room for '%s'\n",
            g_clients[evict_idx].hostname, oldest_ts, hostname);
    memset(&g_clients[evict_idx], 0, sizeof(g_clients[evict_idx]));
    strncpy(g_clients[evict_idx].hostname, hostname, HOSTNAME_LEN - 1);
    return &g_clients[evict_idx];
}

void registry_remove_absent(const char **hostnames, int count) {
    /* Must be called with lock held. Compact the array. */
    int new_count = 0;
    for (int i = 0; i < g_count; i++) {
        bool found = false;
        for (int j = 0; j < count; j++) {
            if (strncmp(g_clients[i].hostname, hostnames[j], HOSTNAME_LEN) == 0) {
                found = true;
                break;
            }
        }
        if (found) {
            if (new_count != i) {
                g_clients[new_count] = g_clients[i];
            }
            new_count++;
        } else {
            fprintf(stderr, "registry: removing absent client '%s'\n", g_clients[i].hostname);
        }
    }
    g_count = new_count;
}

int registry_snapshot(client_info_t *out, int max_count) {
    pthread_mutex_lock(&g_mutex);
    int n = g_count < max_count ? g_count : max_count;
    memcpy(out, g_clients, (size_t)n * sizeof(client_info_t));
    pthread_mutex_unlock(&g_mutex);
    return n;
}

int registry_count(void) {
    pthread_mutex_lock(&g_mutex);
    int n = g_count;
    pthread_mutex_unlock(&g_mutex);
    return n;
}

/* ============================================================
 * Sprint 006 additions
 * ============================================================ */

#include <ctype.h>
#include <stdlib.h>

/* ---- service helpers (T008) ---- */

static int strcaseeq(const char *a, const char *b) {
    if (!a || !b) return 0;
    while (*a && *b) {
        int ca = tolower((unsigned char)*a);
        int cb = tolower((unsigned char)*b);
        if (ca != cb) return 0;
        a++; b++;
    }
    return *a == 0 && *b == 0;
}

service_state_t service_parse_state(const char *s) {
    if (!s || !*s) return SERVICE_STATE_UNKNOWN;
    if (strcaseeq(s, "ok"))          return SERVICE_STATE_OK;
    if (strcaseeq(s, "unhealthy"))   return SERVICE_STATE_UNHEALTHY;
    if (strcaseeq(s, "maintenance")) return SERVICE_STATE_MAINTENANCE;
    if (strcaseeq(s, "down"))        return SERVICE_STATE_DOWN;
    return SERVICE_STATE_UNKNOWN;
}

/* WI #902. A table rather than a payload field, and deliberately a short one:
 * the item asks for a reusable Service Card but also to resist
 * over-generalising on the first consumer, and a second daily feed will teach
 * more about the right shape than speculation will. Match on the service NAME
 * only — identity is (name, host), but a feed's cadence is a property of the
 * feed, not of where it ran. */
static const struct {
    const char *name;
    double window_s;
} g_service_windows[] = {
    { "kmon", SERVICE_DAILY_FRESH_SECONDS },
};

double service_fresh_window(const char *name) {
    if (name) {
        for (size_t i = 0; i < sizeof(g_service_windows) / sizeof(g_service_windows[0]); i++) {
            if (strcmp(name, g_service_windows[i].name) == 0)
                return g_service_windows[i].window_s;
        }
    }
    return SERVICE_FRESH_SECONDS;
}

service_color_t service_color(const service_entry_t *e, double now) {
    if (!e) return SERVICE_COLOR_GRAY;
    if (e->last_valid_state == SERVICE_STATE_DOWN)    return SERVICE_COLOR_GRAY;
    if (e->last_valid_state == SERVICE_STATE_UNKNOWN) return SERVICE_COLOR_GRAY;
    int fresh = (now - e->last_payload_ts) < service_fresh_window(e->name);
    if (!fresh) return SERVICE_COLOR_RED;
    switch (e->last_valid_state) {
        case SERVICE_STATE_OK:          return SERVICE_COLOR_GREEN;
        case SERVICE_STATE_UNHEALTHY:   return SERVICE_COLOR_YELLOW;
        case SERVICE_STATE_MAINTENANCE: return SERVICE_COLOR_BLUE;
        default:                        return SERVICE_COLOR_GRAY;
    }
}

/* ---- service registry (T022) ---- */

static service_entry_t g_services[SERVICE_REGISTRY_MAX];
static int g_service_count = 0;
static pthread_mutex_t g_service_mutex = PTHREAD_MUTEX_INITIALIZER;

service_entry_t *service_registry_find_or_create(const char *name, const char *host) {
    if (!name || !*name || !host || !*host) return NULL;
    pthread_mutex_lock(&g_service_mutex);
    for (int i = 0; i < g_service_count; i++) {
        if (strncmp(g_services[i].name, name, sizeof(g_services[i].name)) == 0 &&
            strncmp(g_services[i].host, host, sizeof(g_services[i].host)) == 0) {
            service_entry_t *r = &g_services[i];
            pthread_mutex_unlock(&g_service_mutex);
            return r;
        }
    }
    if (g_service_count >= SERVICE_REGISTRY_MAX) {
        pthread_mutex_unlock(&g_service_mutex);
        return NULL;
    }
    service_entry_t *e = &g_services[g_service_count++];
    memset(e, 0, sizeof(*e));
    strncpy(e->name, name, sizeof(e->name) - 1);
    strncpy(e->host, host, sizeof(e->host) - 1);
    e->icon_index = -1;
    e->last_valid_state = SERVICE_STATE_UNKNOWN;
    pthread_mutex_unlock(&g_service_mutex);
    return e;
}

void service_registry_apply_payload(service_entry_t *e, const service_entry_t *parsed) {
    if (!e || !parsed) return;
    pthread_mutex_lock(&g_service_mutex);
    /* Identity fields (name, host) intentionally not copied — they are
     * pinned at create time from the Redis key segments. */
    e->last_valid_state = parsed->last_valid_state;
    e->last_payload_ts = parsed->last_payload_ts;
    memcpy(e->text, parsed->text, sizeof(e->text));
    e->icon_index = parsed->icon_index;
    pthread_mutex_unlock(&g_service_mutex);
}

int service_registry_snapshot(service_entry_t *out, int max) {
    pthread_mutex_lock(&g_service_mutex);
    int n = g_service_count < max ? g_service_count : max;
    memcpy(out, g_services, (size_t)n * sizeof(service_entry_t));
    pthread_mutex_unlock(&g_service_mutex);
    return n;
}

/* ---- apt-temps registry (WI #364) ---- */

/* WI #2244: the thresholds and the classification are libkdash's now; only the
 * band -> colour mapping below is still kpidash's, because CD-10 says the
 * library carries no colours.
 *
 * Freshness stays on this side deliberately. kdash_apttemps_band() takes
 * `stale` as a decision the caller has already made rather than a timestamp it
 * judges, so APTTEMPS_FRESH_SECONDS remains the dashboard's own window — it
 * happens to agree with the library's KDASH_APTTEMPS_WINDOW_S (300), and
 * nothing here depends on that continuing to be true. */
apttemps_color_t apttemps_color(const apttemps_entry_t *e, double now) {
    if (!e) return APTTEMPS_COLOR_GRAY;

    /* An entry that never took a payload is treated as stale rather than as a
     * 0 degree room — same answer the explicit `!e->valid` guard gave before. */
    const bool stale = !e->valid ||
                       (now - e->last_payload_ts) >= APTTEMPS_FRESH_SECONDS;

    switch (kdash_apttemps_band(e->temp_f, stale, KDASH_APTTEMPS_COLD_F,
                                KDASH_APTTEMPS_OK_F, KDASH_APTTEMPS_HOT_F)) {
        case KDASH_TEMP_COLD:  return APTTEMPS_COLOR_BLUE;
        case KDASH_TEMP_OK:    return APTTEMPS_COLOR_GREEN;
        case KDASH_TEMP_WARM:  return APTTEMPS_COLOR_ORANGE;
        case KDASH_TEMP_HOT:   return APTTEMPS_COLOR_RED;
        case KDASH_TEMP_STALE:
        default:               return APTTEMPS_COLOR_GRAY;
    }
}

static apttemps_entry_t g_apttemps[APTTEMPS_REGISTRY_MAX];
static int g_apttemps_count = 0;
static pthread_mutex_t g_apttemps_mutex = PTHREAD_MUTEX_INITIALIZER;

apttemps_entry_t *apttemps_registry_find_or_create(const char *slug) {
    if (!slug || !*slug) return NULL;
    pthread_mutex_lock(&g_apttemps_mutex);
    for (int i = 0; i < g_apttemps_count; i++) {
        if (strncmp(g_apttemps[i].slug, slug, sizeof(g_apttemps[i].slug)) == 0) {
            apttemps_entry_t *r = &g_apttemps[i];
            pthread_mutex_unlock(&g_apttemps_mutex);
            return r;
        }
    }
    if (g_apttemps_count >= APTTEMPS_REGISTRY_MAX) {
        pthread_mutex_unlock(&g_apttemps_mutex);
        return NULL;
    }
    apttemps_entry_t *e = &g_apttemps[g_apttemps_count++];
    memset(e, 0, sizeof(*e));
    strncpy(e->slug, slug, sizeof(e->slug) - 1);
    pthread_mutex_unlock(&g_apttemps_mutex);
    return e;
}

void apttemps_registry_apply_payload(apttemps_entry_t *e, const apttemps_entry_t *parsed) {
    if (!e || !parsed) return;
    pthread_mutex_lock(&g_apttemps_mutex);
    e->temp_f = parsed->temp_f;
    e->humidity_pct = parsed->humidity_pct;
    e->last_payload_ts = parsed->last_payload_ts;
    e->valid = true;
    memcpy(e->zone, parsed->zone, sizeof(e->zone));
    pthread_mutex_unlock(&g_apttemps_mutex);
}

int apttemps_registry_snapshot(apttemps_entry_t *out, int max) {
    pthread_mutex_lock(&g_apttemps_mutex);
    int n = g_apttemps_count < max ? g_apttemps_count : max;
    memcpy(out, g_apttemps, (size_t)n * sizeof(apttemps_entry_t));
    pthread_mutex_unlock(&g_apttemps_mutex);
    return n;
}

/* ---- WI #374: remove-by-identity (returns the card container to destroy) ---- */

void *service_registry_remove(const char *name, const char *host) {
    if (!name || !host) return NULL;
    void *container = NULL;
    pthread_mutex_lock(&g_service_mutex);
    for (int i = 0; i < g_service_count; i++) {
        if (strncmp(g_services[i].name, name, sizeof(g_services[i].name)) == 0 &&
            strncmp(g_services[i].host, host, sizeof(g_services[i].host)) == 0) {
            container = g_services[i].container;
            for (int k = i; k < g_service_count - 1; k++)
                g_services[k] = g_services[k + 1];
            g_service_count--;
            break;
        }
    }
    pthread_mutex_unlock(&g_service_mutex);
    return container;
}

void *apttemps_registry_remove(const char *slug) {
    if (!slug) return NULL;
    void *container = NULL;
    pthread_mutex_lock(&g_apttemps_mutex);
    for (int i = 0; i < g_apttemps_count; i++) {
        if (strncmp(g_apttemps[i].slug, slug, sizeof(g_apttemps[i].slug)) == 0) {
            container = g_apttemps[i].container;
            for (int k = i; k < g_apttemps_count - 1; k++)
                g_apttemps[k] = g_apttemps[k + 1];
            g_apttemps_count--;
            break;
        }
    }
    pthread_mutex_unlock(&g_apttemps_mutex);
    return container;
}

/* ---- host staleness registry (WI #1903) ----
 *
 * Rebuilt from the feed each poll, because the feed is presence-owned: a key
 * that stops existing is the only all-clear. Observations land in a PENDING
 * set and only replace the live set on commit, so a scan that dies half way
 * can abort and leave the cards standing. Committing a partial scan would
 * take down cards nothing had cleared, and a host with no card reads as
 * healthy — the one wrong answer this feed exists to prevent (kdashdata
 * CD-18).
 *
 * The pending set carries no LVGL handles: those belong to the live entries
 * and must survive every cycle in which their host stays stale. */

typedef struct {
    char host[64];
    stale_deployer_t deployers[STALE_DEPLOYERS_MAX];
    int deployer_count;
} stale_pending_t;

static stale_entry_t g_stale[STALE_REGISTRY_MAX];
static int g_stale_count = 0;
static stale_pending_t g_stale_pending[STALE_REGISTRY_MAX];
static int g_stale_pending_count = 0;
static pthread_mutex_t g_stale_mutex = PTHREAD_MUTEX_INITIALIZER;

void stale_registry_begin_cycle(void) {
    pthread_mutex_lock(&g_stale_mutex);
    g_stale_pending_count = 0;
    pthread_mutex_unlock(&g_stale_mutex);
}

void stale_registry_abort_cycle(void) {
    /* Discard the pending set and leave the live one exactly as it was. */
    pthread_mutex_lock(&g_stale_mutex);
    g_stale_pending_count = 0;
    pthread_mutex_unlock(&g_stale_mutex);
}

void stale_registry_observe(const char *host, const char *deployer, double since,
                            bool since_known) {
    if (!host || !*host) return;
    /* The panel's own host: if rpi53 is behind there is no dashboard. */
    if (strcmp(host, STALE_EXCLUDED_HOST) == 0) return;
    if (!deployer || !*deployer) deployer = STALE_DEPLOYER_UNKNOWN;

    pthread_mutex_lock(&g_stale_mutex);
    stale_pending_t *p = NULL;
    for (int i = 0; i < g_stale_pending_count; i++) {
        if (strncmp(g_stale_pending[i].host, host, sizeof(g_stale_pending[i].host)) == 0) {
            p = &g_stale_pending[i];
            break;
        }
    }
    if (!p) {
        if (g_stale_pending_count >= STALE_REGISTRY_MAX) {
            pthread_mutex_unlock(&g_stale_mutex);
            return;
        }
        p = &g_stale_pending[g_stale_pending_count++];
        memset(p, 0, sizeof(*p));
        strncpy(p->host, host, sizeof(p->host) - 1);
    }
    /* SCAN may return the same key on two cursor pages — list it once. */
    for (int i = 0; i < p->deployer_count; i++) {
        if (strncmp(p->deployers[i].name, deployer, sizeof(p->deployers[i].name)) == 0) {
            pthread_mutex_unlock(&g_stale_mutex);
            return;
        }
    }
    if (p->deployer_count >= STALE_DEPLOYERS_MAX) {
        pthread_mutex_unlock(&g_stale_mutex);
        return;
    }
    stale_deployer_t *d = &p->deployers[p->deployer_count++];
    memset(d, 0, sizeof(*d));
    strncpy(d->name, deployer, sizeof(d->name) - 1);
    d->since = since;
    d->since_known = since_known;
    pthread_mutex_unlock(&g_stale_mutex);
}

/* Oldest `since` first (WI #1903). A deployer whose payload could not be read
 * carries no ordering information, so it sorts after every known one rather
 * than claiming to be the oldest; equal keys break by name so the card body
 * does not reshuffle between polls. */
static int stale_deployer_cmp(const stale_deployer_t *a, const stale_deployer_t *b) {
    if (a->since_known != b->since_known) return a->since_known ? -1 : 1;
    if (a->since_known) {
        if (a->since < b->since) return -1;
        if (a->since > b->since) return 1;
    }
    return strncmp(a->name, b->name, sizeof(a->name));
}

static void stale_sort_deployers(stale_deployer_t *d, int n) {
    for (int i = 1; i < n; i++) {
        stale_deployer_t tmp = d[i];
        int j = i;
        while (j > 0 && stale_deployer_cmp(&d[j - 1], &tmp) > 0) {
            d[j] = d[j - 1];
            j--;
        }
        d[j] = tmp;
    }
}

void stale_registry_commit_cycle(void) {
    pthread_mutex_lock(&g_stale_mutex);

    /* Stable, name-ascending host order so the snapshot (and the cards built
     * from it) do not depend on SCAN order. */
    for (int i = 1; i < g_stale_pending_count; i++) {
        stale_pending_t tmp = g_stale_pending[i];
        int j = i;
        while (j > 0 && strncmp(g_stale_pending[j - 1].host, tmp.host, sizeof(tmp.host)) > 0) {
            g_stale_pending[j] = g_stale_pending[j - 1];
            j--;
        }
        g_stale_pending[j] = tmp;
    }
    for (int i = 0; i < g_stale_pending_count; i++)
        stale_sort_deployers(g_stale_pending[i].deployers, g_stale_pending[i].deployer_count);

    /* Existing hosts: adopt the pending deployer list, or empty it if the
     * host was not seen. An emptied entry keeps its LVGL handles until
     * stale_registry_reap hands them back for destruction on the LVGL
     * thread — registry.c must never touch LVGL itself. */
    for (int i = 0; i < g_stale_count; i++) {
        const stale_pending_t *p = NULL;
        for (int k = 0; k < g_stale_pending_count; k++) {
            if (strncmp(g_stale_pending[k].host, g_stale[i].host, sizeof(g_stale[i].host)) == 0) {
                p = &g_stale_pending[k];
                break;
            }
        }
        if (p) {
            memcpy(g_stale[i].deployers, p->deployers, sizeof(g_stale[i].deployers));
            g_stale[i].deployer_count = p->deployer_count;
        } else {
            memset(g_stale[i].deployers, 0, sizeof(g_stale[i].deployers));
            g_stale[i].deployer_count = 0;
        }
    }

    /* Hosts that are newly stale. */
    for (int k = 0; k < g_stale_pending_count; k++) {
        bool known = false;
        for (int i = 0; i < g_stale_count; i++) {
            if (strncmp(g_stale[i].host, g_stale_pending[k].host, sizeof(g_stale[i].host)) == 0) {
                known = true;
                break;
            }
        }
        if (known) continue;
        if (g_stale_count >= STALE_REGISTRY_MAX) break;
        stale_entry_t *e = &g_stale[g_stale_count++];
        memset(e, 0, sizeof(*e));
        /* Both are char[64] and the pending host was NUL-terminated on the way
         * in, so this is an exact copy — strncpy here trips
         * -Wstringop-truncation under -O2, which the release build treats as
         * noise it should not have to read. */
        memcpy(e->host, g_stale_pending[k].host, sizeof(e->host));
        memcpy(e->deployers, g_stale_pending[k].deployers, sizeof(e->deployers));
        e->deployer_count = g_stale_pending[k].deployer_count;
    }

    g_stale_pending_count = 0;
    pthread_mutex_unlock(&g_stale_mutex);
}

int stale_registry_snapshot(stale_entry_t *out, int max) {
    if (!out || max <= 0) return 0;
    pthread_mutex_lock(&g_stale_mutex);
    int n = g_stale_count < max ? g_stale_count : max;
    memcpy(out, g_stale, (size_t)n * sizeof(stale_entry_t));
    pthread_mutex_unlock(&g_stale_mutex);
    return n;
}

stale_entry_t *stale_registry_find(const char *host) {
    if (!host || !*host) return NULL;
    pthread_mutex_lock(&g_stale_mutex);
    stale_entry_t *r = NULL;
    for (int i = 0; i < g_stale_count; i++) {
        if (strncmp(g_stale[i].host, host, sizeof(g_stale[i].host)) == 0) {
            r = &g_stale[i];
            break;
        }
    }
    pthread_mutex_unlock(&g_stale_mutex);
    return r;
}

int stale_registry_reap(void **out_containers, int max) {
    if (!out_containers || max <= 0) return 0;
    int n = 0;
    pthread_mutex_lock(&g_stale_mutex);
    for (int i = 0; i < g_stale_count;) {
        if (g_stale[i].deployer_count == 0 && n < max) {
            out_containers[n++] = g_stale[i].container;
            for (int k = i; k < g_stale_count - 1; k++) g_stale[k] = g_stale[k + 1];
            g_stale_count--;
            continue;
        }
        i++;
    }
    pthread_mutex_unlock(&g_stale_mutex);
    return n;
}

void stale_format_title(const stale_entry_t *e, char *buf, size_t n) {
    if (!buf || n == 0) return;
    buf[0] = '\0';
    if (!e) return;
    snprintf(buf, n, "%s stale", e->host);
}

void stale_format_body(const stale_entry_t *e, char *buf, size_t n) {
    if (!buf || n == 0) return;
    buf[0] = '\0';
    if (!e) return;
    size_t used = 0;
    for (int i = 0; i < e->deployer_count; i++) {
        int w = snprintf(buf + used, n - used, "%s%s", i ? "\n" : "", e->deployers[i].name);
        if (w < 0 || (size_t)w >= n - used) break;
        used += (size_t)w;
    }
}

/* ---- graph host series (T006) ---- */

static graph_host_series_t g_graph_hosts[GRAPH_HOST_MAX];
static int g_graph_host_count = 0;
static pthread_mutex_t g_graph_mutex = PTHREAD_MUTEX_INITIALIZER;

graph_host_series_t *graph_host_find_or_create(const char *host) {
    if (!host || !*host) return NULL;
    pthread_mutex_lock(&g_graph_mutex);
    for (int i = 0; i < g_graph_host_count; i++) {
        if (strncmp(g_graph_hosts[i].host, host, sizeof(g_graph_hosts[i].host)) == 0) {
            graph_host_series_t *r = &g_graph_hosts[i];
            pthread_mutex_unlock(&g_graph_mutex);
            return r;
        }
    }
    if (g_graph_host_count >= GRAPH_HOST_MAX) {
        pthread_mutex_unlock(&g_graph_mutex);
        return NULL;
    }
    graph_host_series_t *s = &g_graph_hosts[g_graph_host_count++];
    memset(s, 0, sizeof(*s));
    strncpy(s->host, host, sizeof(s->host) - 1);
    s->host[sizeof(s->host) - 1] = '\0';
    pthread_mutex_unlock(&g_graph_mutex);
    return s;
}

void graph_host_touch(const char *host, double ts) {
    if (!host || !*host) return;
    pthread_mutex_lock(&g_graph_mutex);
    for (int i = 0; i < g_graph_host_count; i++) {
        if (strncmp(g_graph_hosts[i].host, host, sizeof(g_graph_hosts[i].host)) == 0) {
            g_graph_hosts[i].last_sample_ts = ts;
            break;
        }
    }
    pthread_mutex_unlock(&g_graph_mutex);
}

bool graph_host_is_stale(const graph_host_series_t *s, double now) {
    if (!s) return true;
    return (now - s->last_sample_ts) >= GRAPH_HOST_STALE_SECONDS;
}

int graph_host_snapshot(graph_host_series_t *out, int max) {
    pthread_mutex_lock(&g_graph_mutex);
    int n = g_graph_host_count < max ? g_graph_host_count : max;
    memcpy(out, g_graph_hosts, (size_t)n * sizeof(graph_host_series_t));
    pthread_mutex_unlock(&g_graph_mutex);
    return n;
}

bool graph_host_remove(const char *host) {
    if (!host || !*host) return false;
    pthread_mutex_lock(&g_graph_mutex);
    for (int i = 0; i < g_graph_host_count; i++) {
        if (strncmp(g_graph_hosts[i].host, host, sizeof(g_graph_hosts[i].host)) == 0) {
            for (int k = i; k < g_graph_host_count - 1; k++) {
                g_graph_hosts[k] = g_graph_hosts[k + 1];
            }
            g_graph_host_count--;
            pthread_mutex_unlock(&g_graph_mutex);
            return true;
        }
    }
    pthread_mutex_unlock(&g_graph_mutex);
    return false;
}

/* ---- layout pool (T005) ---- */

static int cmp_widget_request_stable(const void *a, const void *b) {
    /* qsort is not guaranteed stable; we get stability by recording
     * original index in a wrapper. See layout_pool_place(). */
    const widget_request_t *wa = (const widget_request_t *)a;
    const widget_request_t *wb = (const widget_request_t *)b;
    if (wa->kind < wb->kind) return -1;
    if (wa->kind > wb->kind) return 1;
    return 0;
}

int layout_pool_place(const widget_request_t *requests, size_t n_requests,
                      widget_request_t *out_placed, size_t out_cap) {
    if (!requests || !out_placed || n_requests == 0 || out_cap == 0) return 0;

    /* Stable sort: pair each request with its original index, sort by
     * (kind, original_index). */
    typedef struct { widget_request_t r; size_t orig; } pair_t;
    pair_t *pairs = (pair_t *)malloc(n_requests * sizeof(pair_t));
    if (!pairs) return 0;
    for (size_t i = 0; i < n_requests; i++) {
        pairs[i].r = requests[i];
        pairs[i].orig = i;
    }
    /* simple stable insertion sort (n is small — at most ~16) */
    for (size_t i = 1; i < n_requests; i++) {
        pair_t tmp = pairs[i];
        size_t j = i;
        while (j > 0 && (pairs[j - 1].r.kind > tmp.r.kind ||
                         (pairs[j - 1].r.kind == tmp.r.kind &&
                          pairs[j - 1].orig > tmp.orig))) {
            pairs[j] = pairs[j - 1];
            j--;
        }
        pairs[j] = tmp;
    }

    /* Greedy place with drop-and-continue. */
    int placed = 0;
    int remaining = LAYOUT_POOL_CAPACITY_CELLS;
    for (size_t i = 0; i < n_requests && (size_t)placed < out_cap; i++) {
        int cells = pairs[i].r.cells;
        if (cells <= 0) continue;
        if (cells > remaining) continue; /* drop, try next (lower priority may fit) */
        out_placed[placed++] = pairs[i].r;
        remaining -= cells;
        if (remaining == 0) break;
    }
    free(pairs);
    (void)cmp_widget_request_stable; /* retained for potential future use */
    return placed;
}
