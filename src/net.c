#include "net.h"
#include "protocol.h"

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <cjson/cJSON.h>
#include <stdlib.h>

typedef struct {
    registry_t *reg;
    int port;
} net_ctx_t;

static void handle_message(registry_t *reg, const char *buf, int len) {
    cJSON *root = cJSON_ParseWithLength(buf, (size_t)len);
    if (!root) {
        fprintf(stderr, "net: malformed JSON\n");
        return;
    }

    cJSON *type_obj = cJSON_GetObjectItemCaseSensitive(root, "type");
    cJSON *host_obj = cJSON_GetObjectItemCaseSensitive(root, "host");

    if (!cJSON_IsString(type_obj) || !cJSON_IsString(host_obj)) {
        fprintf(stderr, "net: missing type or host field\n");
        cJSON_Delete(root);
        return;
    }

    const char *type = type_obj->valuestring;
    const char *host = host_obj->valuestring;

    registry_lock(reg);
    client_info_t *client = registry_find_or_create(reg, host);
    if (!client) {
        registry_unlock(reg);
        cJSON_Delete(root);
        return;
    }

    if (strcmp(type, MSG_TYPE_HEALTH) == 0) {
        client->last_health = time(NULL);
        cJSON *uptime_obj = cJSON_GetObjectItemCaseSensitive(root, "uptime");
        if (cJSON_IsNumber(uptime_obj)) {
            client->uptime = uptime_obj->valuedouble;
        } else if (cJSON_IsString(uptime_obj)) {
            client->uptime = strtod(uptime_obj->valuestring, NULL);
        }
    } else if (strcmp(type, MSG_TYPE_TASK) == 0) {
        cJSON *task_obj = cJSON_GetObjectItemCaseSensitive(root, "task");
        cJSON *started_obj = cJSON_GetObjectItemCaseSensitive(root, "started");

        if (cJSON_IsString(task_obj)) {
            strncpy(client->task, task_obj->valuestring, TASK_LEN - 1);
            client->task[TASK_LEN - 1] = '\0';
        }
        if (cJSON_IsNumber(started_obj)) {
            client->task_start = (time_t)started_obj->valuedouble;
        } else if (cJSON_IsString(started_obj)) {
            client->task_start = (time_t)strtol(started_obj->valuestring, NULL, 10);
        }
    } else if (strcmp(type, MSG_TYPE_TASK_DONE) == 0) {
        /* Complete current task — archive it and clear */
        if (client->task[0] != '\0') {
            strncpy(client->last_task, client->task, TASK_LEN - 1);
            client->last_task[TASK_LEN - 1] = '\0';
            client->last_task_completed = time(NULL);
        }
        client->task[0] = '\0';
        client->task_start = 0;
    }

    registry_unlock(reg);
    cJSON_Delete(root);
}

static void *udp_listener(void *arg) {
    net_ctx_t *ctx = (net_ctx_t *)arg;

    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) {
        perror("net: socket");
        return NULL;
    }

    int optval = 1;
    setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons((uint16_t)ctx->port);
    addr.sin_addr.s_addr = INADDR_ANY;

    if (bind(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("net: bind");
        close(sock);
        return NULL;
    }

    printf("net: listening on UDP port %d\n", ctx->port);

    char buf[MSG_BUF_SIZE];
    while (1) {
        struct sockaddr_in src;
        socklen_t src_len = sizeof(src);
        ssize_t n = recvfrom(sock, buf, sizeof(buf) - 1, 0,
                             (struct sockaddr *)&src, &src_len);
        if (n <= 0) continue;
        buf[n] = '\0';
        handle_message(ctx->reg, buf, (int)n);
    }

    close(sock);
    return NULL;
}

int net_start(registry_t *reg, int port) {
    /* Allocated once, lives for program lifetime */
    static net_ctx_t ctx;
    ctx.reg = reg;
    ctx.port = port;

    pthread_t tid;
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);

    int rc = pthread_create(&tid, &attr, udp_listener, &ctx);
    pthread_attr_destroy(&attr);

    if (rc != 0) {
        fprintf(stderr, "net: failed to create UDP thread\n");
        return -1;
    }

    return 0;
}
