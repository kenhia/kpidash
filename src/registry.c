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
