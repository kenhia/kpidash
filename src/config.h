#ifndef CONFIG_H
#define CONFIG_H

#include "protocol.h"

typedef struct {
    char redis_host[128];
    int redis_port;
    char redis_auth[256]; /* from REDISCLI_AUTH */
    char drm_dev[256];
    int max_clients;    /* from KPIDASH_MAX_CLIENTS, bounded by MAX_CLIENTS */
    int activity_max;   /* from KPIDASH_ACTIVITY_MAX */
    char log_file[512]; /* from KPIDASH_LOG_FILE */
    /* WI #3012: where the admitted-hosts file lives. One hostname per line;
     * the record of which hosts have ever published, so a host that is down
     * across a restart still gets a (red) card. /var/lib is the FHS answer
     * for service state and parallels the log path above. */
    char state_file[512]; /* from KPIDASH_STATE_FILE */
    char priority_clients[PRIORITY_CLIENTS_MAX][HOSTNAME_LEN]; /* from KPIDASH_PRIORITY_CLIENTS */
    int priority_client_count;
    /* WI #4062: the network card's columns, in order (KPIDASH_NET_HOSTS). A
     * fixed list rather than whoever publishes, because a host that never
     * publishes must still get a "no feed" column rather than vanish. */
    char net_hosts[NET_HOSTS_CONFIG_MAX][HOSTNAME_LEN];
    int net_host_count;
    /* WI #4111: hosts whose staleness card waits out STALE_INTERMITTENT_GRACE_S
     * (KPIDASH_INTERMITTENT_HOSTS). */
    char intermittent_hosts[INTERMITTENT_HOSTS_MAX][HOSTNAME_LEN];
    int intermittent_host_count;
} kpidash_config_t;

/**
 * Load configuration from environment variables.
 * All fields are populated; unset variables use their defaults.
 */
void config_load(kpidash_config_t *cfg);

#endif /* CONFIG_H */
