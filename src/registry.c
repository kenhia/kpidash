#include "registry.h"
#include <string.h>
#include <stdio.h>

void registry_init(registry_t *reg) {
    memset(reg, 0, sizeof(*reg));
    pthread_mutex_init(&reg->mutex, NULL);
}

void registry_lock(registry_t *reg) {
    pthread_mutex_lock(&reg->mutex);
}

void registry_unlock(registry_t *reg) {
    pthread_mutex_unlock(&reg->mutex);
}

client_info_t *registry_find_or_create(registry_t *reg, const char *hostname) {
    /* Search existing */
    for (int i = 0; i < reg->count; i++) {
        if (reg->clients[i].active &&
            strncmp(reg->clients[i].hostname, hostname, HOSTNAME_LEN) == 0) {
            return &reg->clients[i];
        }
    }

    /* Allocate new slot */
    if (reg->count >= MAX_CLIENTS) {
        fprintf(stderr, "registry: max clients (%d) reached\n", MAX_CLIENTS);
        return NULL;
    }

    client_info_t *c = &reg->clients[reg->count];
    memset(c, 0, sizeof(*c));
    strncpy(c->hostname, hostname, HOSTNAME_LEN - 1);
    c->hostname[HOSTNAME_LEN - 1] = '\0';
    c->active = true;
    reg->count++;

    printf("registry: new client '%s' (slot %d)\n", c->hostname, reg->count - 1);
    return c;
}
