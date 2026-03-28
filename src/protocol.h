#ifndef PROTOCOL_H
#define PROTOCOL_H

#define KPIDASH_DEFAULT_PORT 5555
#define MAX_CLIENTS          16
#define HOSTNAME_LEN         64
#define TASK_LEN             128
#define MSG_BUF_SIZE         1024
#define HEALTH_TIMEOUT_SEC   10

/* Message type strings (match JSON "type" field) */
#define MSG_TYPE_HEALTH    "health"
#define MSG_TYPE_TASK      "task"
#define MSG_TYPE_TASK_DONE "task_done"

#endif /* PROTOCOL_H */
