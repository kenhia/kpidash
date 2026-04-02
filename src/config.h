#ifndef CONFIG_H
#define CONFIG_H

#include "protocol.h"

typedef struct {
    char redis_host[128];
    int  redis_port;
    char redis_auth[256];   /* from REDISCLI_AUTH */
    char drm_dev[256];
    int  max_clients;       /* from KPIDASH_MAX_CLIENTS, bounded by MAX_CLIENTS */
    int  activity_max;      /* from KPIDASH_ACTIVITY_MAX */
    char log_file[512];     /* from KPIDASH_LOG_FILE */
    char priority_clients[PRIORITY_CLIENTS_MAX][HOSTNAME_LEN]; /* from KPIDASH_PRIORITY_CLIENTS */
    int  priority_client_count;
} kpidash_config_t;

/**
 * Load configuration from environment variables.
 * All fields are populated; unset variables use their defaults.
 */
void config_load(kpidash_config_t *cfg);

#endif /* CONFIG_H */
