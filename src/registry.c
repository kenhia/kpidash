#include "registry.h"

#include <pthread.h>
#include <stdio.h>
#include <string.h>

/* ---- Global singleton ---- */
static client_info_t g_clients[MAX_CLIENTS];
static int g_count = 0;
static pthread_mutex_t g_mutex = PTHREAD_MUTEX_INITIALIZER;

void registry_init(void) {
    pthread_mutex_lock(&g_mutex);
    memset(g_clients, 0, sizeof(g_clients));
    g_count = 0;
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

client_info_t *registry_find_or_create(const char *hostname) {
    /* Search existing (must be called with lock held) */
    for (int i = 0; i < g_count; i++) {
        if (strncmp(g_clients[i].hostname, hostname, HOSTNAME_LEN) == 0) {
            return &g_clients[i];
        }
    }

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

service_color_t service_color(const service_entry_t *e, double now) {
    if (!e) return SERVICE_COLOR_GRAY;
    if (e->last_valid_state == SERVICE_STATE_DOWN)    return SERVICE_COLOR_GRAY;
    if (e->last_valid_state == SERVICE_STATE_UNKNOWN) return SERVICE_COLOR_GRAY;
    int fresh = (now - e->last_payload_ts) < 60.0;
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

service_entry_t *service_registry_find_or_create(const char *name) {
    if (!name || !*name) return NULL;
    pthread_mutex_lock(&g_service_mutex);
    for (int i = 0; i < g_service_count; i++) {
        if (strncmp(g_services[i].name, name, sizeof(g_services[i].name)) == 0) {
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
    e->icon_index = -1;
    e->last_valid_state = SERVICE_STATE_UNKNOWN;
    pthread_mutex_unlock(&g_service_mutex);
    return e;
}

void service_registry_apply_payload(service_entry_t *e, const service_entry_t *parsed) {
    if (!e || !parsed) return;
    pthread_mutex_lock(&g_service_mutex);
    e->last_valid_state = parsed->last_valid_state;
    e->last_payload_ts = parsed->last_payload_ts;
    memcpy(e->text, parsed->text, sizeof(e->text));
    memcpy(e->host, parsed->host, sizeof(e->host));
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
