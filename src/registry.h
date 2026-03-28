#ifndef REGISTRY_H
#define REGISTRY_H

#include <stdbool.h>
#include <time.h>
#include <pthread.h>
#include "lvgl.h"
#include "protocol.h"

typedef struct {
    char hostname[HOSTNAME_LEN];
    time_t last_health;
    double uptime;
    char task[TASK_LEN];
    time_t task_start;
    char last_task[TASK_LEN];       /* most recently completed task */
    time_t last_task_completed;     /* epoch when it was marked done */
    bool active;

    /* LVGL widget handles (only touched from main/LVGL thread) */
    lv_obj_t *container;
    lv_obj_t *status_led;
    lv_obj_t *hostname_label;
    lv_obj_t *task_label;
    lv_obj_t *elapsed_label;
} client_info_t;

typedef struct {
    client_info_t clients[MAX_CLIENTS];
    int count;
    pthread_mutex_t mutex;
} registry_t;

void registry_init(registry_t *reg);
void registry_lock(registry_t *reg);
void registry_unlock(registry_t *reg);

/**
 * Find existing client by hostname, or allocate a new slot.
 * Must be called with the registry locked.
 * Returns NULL if the registry is full.
 */
client_info_t *registry_find_or_create(registry_t *reg, const char *hostname);

#endif /* REGISTRY_H */
