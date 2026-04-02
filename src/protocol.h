#ifndef PROTOCOL_H
#define PROTOCOL_H

/* ---- String size constants ---- */
#define HOSTNAME_LEN      64
#define GPU_NAME_LEN      64
#define LABEL_LEN         64
#define TASK_LEN          128
#define ACTIVITY_ID_LEN   37   /* UUID4 + NUL */
#define ACTIVITY_NAME_LEN 128
#define MSG_BUF_SIZE      4096

/* ---- Capacity limits ---- */
#define MAX_CLIENTS          16
#define MAX_DISKS             8
#define ACTIVITY_MAX_DISPLAY 10
#define PRIORITY_CLIENTS_MAX 16

/* ---- TTL constants (seconds) ---- */
#define HEALTH_TTL_S     5
#define TELEMETRY_TTL_S  15
#define REPOS_TTL_S      30
#define STATUS_ACK_TTL_S 60

/* ---- Fortune rotation interval (seconds, compile-time constant) ---- */
#define FORTUNE_INTERVAL_S 300

/* ---- Application version ---- */
#define KPIDASH_VERSION "1.0.0"

/* ---- Redis key macros (%s = hostname or id placeholder) ---- */
#define KPIDASH_KEY_HEALTH          "kpidash:client:%s:health"
#define KPIDASH_KEY_TELEMETRY       "kpidash:client:%s:telemetry"
#define KPIDASH_KEY_CLIENTS         "kpidash:clients"
#define KPIDASH_KEY_ACTIVITIES      "kpidash:activities"
#define KPIDASH_KEY_ACTIVITY        "kpidash:activity:%s"
#define KPIDASH_KEY_REPOS           "kpidash:repos:%s"
#define KPIDASH_KEY_FORTUNE_CURRENT "kpidash:fortune:current"
#define KPIDASH_KEY_FORTUNE_PUSHED  "kpidash:fortune:pushed"
#define KPIDASH_KEY_STATUS_CURRENT  "kpidash:status:current"
#define KPIDASH_KEY_STATUS_ACK      "kpidash:status:ack:%s"
#define KPIDASH_KEY_LOGPATH         "kpidash:system:logpath"
#define KPIDASH_KEY_VERSION         "kpidash:system:version"

#endif /* PROTOCOL_H */
