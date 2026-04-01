#ifndef STATUS_H
#define STATUS_H

#include <stdbool.h>

typedef enum { STATUS_WARNING, STATUS_ERROR } status_severity_t;

typedef struct {
    char   message_id[37];   /* UUID4 + NUL */
    status_severity_t severity;
    char   message[256];
    double ts;               /* Unix epoch when recorded */
} status_message_t;

/**
 * Push a new warning or error onto the status queue.
 * If the queue was empty, writes to kpidash:status:current via Redis.
 */
void status_push(status_severity_t severity, const char *message);

/**
 * Returns the current head of the queue, or NULL if empty.
 * Does NOT lock — call from LVGL main thread only.
 */
const status_message_t *status_get_current(void);

/**
 * Check kpidash:status:ack:{id} in Redis each poll cycle.
 * Removes the head of the queue if acked; updates status:current.
 * ctx is a redisContext*; passing void* avoids including hiredis.h here.
 */
void status_redis_check_ack(void *ctx);

#endif /* STATUS_H */
