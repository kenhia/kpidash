#include "status.h"

#include <cjson/cJSON.h>
#include <hiredis/hiredis.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "protocol.h"
#include "ui.h"

#define STATUS_QUEUE_MAX 16

/* ---- Simple UUID4-ish generator (not cryptographic) ---- */
static void gen_id(char *out, size_t len) {
    /* Use timestamp + pointer address as entropy for a unique-enough ID */
    snprintf(out, len, "%lx-%lx", (unsigned long)time(NULL), (unsigned long)(uintptr_t)out);
}

/* ---- Queue ---- */
static status_message_t g_queue[STATUS_QUEUE_MAX];
static int g_head = 0;
static int g_tail = 0;
static int g_count = 0;

static bool queue_is_empty(void) {
    return g_count == 0;
}

static void write_current_to_redis(redisContext *ctx, const status_message_t *m) {
    if (!ctx || ctx->err)
        return;
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "message_id", m->message_id);
    cJSON_AddStringToObject(root, "severity", m->severity == STATUS_ERROR ? "error" : "warning");
    cJSON_AddStringToObject(root, "message", m->message);
    cJSON_AddNumberToObject(root, "ts", m->ts);
    char *json = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (json) {
        redisReply *r = redisCommand(ctx, "SET %s %s", KPIDASH_KEY_STATUS_CURRENT, json);
        if (r)
            freeReplyObject(r);
        free(json);
    }
}

static void delete_current_from_redis(redisContext *ctx) {
    if (!ctx || ctx->err)
        return;
    redisReply *r = redisCommand(ctx, "DEL " KPIDASH_KEY_STATUS_CURRENT);
    if (r)
        freeReplyObject(r);
}

void status_push(status_severity_t severity, const char *message) {
    if (g_count >= STATUS_QUEUE_MAX) {
        fprintf(stderr, "status: queue full, dropping: %s\n", message);
        return;
    }
    status_message_t *m = &g_queue[g_tail % STATUS_QUEUE_MAX];
    gen_id(m->message_id, sizeof(m->message_id));
    m->severity = severity;
    strncpy(m->message, message, sizeof(m->message) - 1);
    m->message[sizeof(m->message) - 1] = '\0';
    m->ts = (double)time(NULL);
    g_tail = (g_tail + 1) % STATUS_QUEUE_MAX;
    g_count++;

    /* Show status bar if this is now the head */
    if (g_count == 1) {
        ui_status_bar_show(severity, message);
    }
}

const status_message_t *status_get_current(void) {
    if (queue_is_empty())
        return NULL;
    return &g_queue[g_head % STATUS_QUEUE_MAX];
}

void status_redis_check_ack(void *ctx_void) {
    redisContext *ctx = (redisContext *)ctx_void;
    if (!ctx || ctx->err)
        return;

    const status_message_t *current = status_get_current();
    if (!current)
        return;

    char ack_key[256];
    snprintf(ack_key, sizeof(ack_key), KPIDASH_KEY_STATUS_ACK, current->message_id);

    redisReply *r = redisCommand(ctx, "EXISTS %s", ack_key);
    if (r && r->type == REDIS_REPLY_INTEGER && r->integer == 1) {
        freeReplyObject(r);
        /* Delete ack key */
        r = redisCommand(ctx, "DEL %s", ack_key);
        if (r)
            freeReplyObject(r);

        /* Advance queue */
        g_head = (g_head + 1) % STATUS_QUEUE_MAX;
        g_count--;

        if (g_count == 0) {
            delete_current_from_redis(ctx);
            ui_status_bar_hide();
        } else {
            const status_message_t *next = status_get_current();
            write_current_to_redis(ctx, next);
            ui_status_bar_show(next->severity, next->message);
        }
    } else {
        if (r)
            freeReplyObject(r);
    }
}
