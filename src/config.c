#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int parse_int_env(const char *name, int default_val, int min_val, int max_val) {
    const char *v = getenv(name);
    if (!v)
        return default_val;
    int n = atoi(v);
    if (n < min_val || n > max_val) {
        fprintf(stderr, "config: %s=%s out of range [%d,%d], using default %d\n", name, v, min_val,
                max_val, default_val);
        return default_val;
    }
    return n;
}

static void parse_str_env(const char *name, const char *default_val, char *out, size_t out_len) {
    const char *v = getenv(name);
    strncpy(out, v ? v : default_val, out_len - 1);
    out[out_len - 1] = '\0';
}

/* Comma-separated hostnames into out[], spaces trimmed, empty tokens dropped.
 * Returns the count. A variable set to "" yields zero hosts, not the default. */
static int parse_host_list_env(const char *name, const char *default_val,
                               char (*out)[HOSTNAME_LEN], int max) {
    char buf[16 * HOSTNAME_LEN];
    parse_str_env(name, default_val, buf, sizeof(buf));
    int count = 0;
    char *save = NULL;
    for (char *tok = strtok_r(buf, ",", &save); tok && count < max;
         tok = strtok_r(NULL, ",", &save)) {
        while (*tok == ' ')
            tok++;
        size_t len = strnlen(tok, HOSTNAME_LEN - 1);
        while (len > 0 && tok[len - 1] == ' ')
            len--;
        if (len == 0)
            continue;
        memcpy(out[count], tok, len);
        out[count][len] = '\0';
        count++;
    }
    return count;
}

void config_load(kpidash_config_t *cfg) {
    memset(cfg, 0, sizeof(*cfg));

    parse_str_env("KPIDASH_REDIS_HOST", "127.0.0.1", cfg->redis_host, sizeof(cfg->redis_host));

    cfg->redis_port = parse_int_env("KPIDASH_REDIS_PORT", 6379, 1, 65535);

    const char *auth = getenv("REDISCLI_AUTH");
    if (auth)
        strncpy(cfg->redis_auth, auth, sizeof(cfg->redis_auth) - 1);

    parse_str_env("KPIDASH_DRM_DEV", "/dev/dri/card1", cfg->drm_dev, sizeof(cfg->drm_dev));

    cfg->max_clients = parse_int_env("KPIDASH_MAX_CLIENTS", MAX_CLIENTS, 1, MAX_CLIENTS);
    cfg->activity_max = parse_int_env("KPIDASH_ACTIVITY_MAX", ACTIVITY_MAX_DISPLAY, 1, 100);

    parse_str_env("KPIDASH_LOG_FILE", "/var/log/kpidash/kpidash.log", cfg->log_file,
                  sizeof(cfg->log_file));

    parse_str_env("KPIDASH_STATE_FILE", "/var/lib/kpidash/admitted", cfg->state_file,
                  sizeof(cfg->state_file));

    /* WI #4062: network-card hosts, comma-separated, card order. */
    cfg->net_host_count = parse_host_list_env("KPIDASH_NET_HOSTS", NET_HOSTS_DEFAULT,
                                              cfg->net_hosts, NET_HOSTS_CONFIG_MAX);

    /* WI #4111: intermittent hosts, comma-separated. */
    cfg->intermittent_host_count =
        parse_host_list_env("KPIDASH_INTERMITTENT_HOSTS", INTERMITTENT_HOSTS_DEFAULT,
                            cfg->intermittent_hosts, INTERMITTENT_HOSTS_MAX);

    /* Parse priority clients: comma-separated hostnames */
    const char *pc = getenv("KPIDASH_PRIORITY_CLIENTS");
    if (pc) {
        char buf[sizeof(cfg->priority_clients[0]) * PRIORITY_CLIENTS_MAX];
        strncpy(buf, pc, sizeof(buf) - 1);
        buf[sizeof(buf) - 1] = '\0';
        char *tok = strtok(buf, ",");
        while (tok && cfg->priority_client_count < PRIORITY_CLIENTS_MAX) {
            /* trim leading spaces */
            while (*tok == ' ') tok++;
            strncpy(cfg->priority_clients[cfg->priority_client_count], tok, HOSTNAME_LEN - 1);
            cfg->priority_client_count++;
            tok = strtok(NULL, ",");
        }
    }
}
