#include "redis.h"

#include <cjson/cJSON.h>
#include <hiredis/hiredis.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <kdash/kdash_keys.h>

#include "hosthist.h"
#include "kkstate.h"
#include "limstate.h"
#include "memstat.h"
#include "nasstate.h"
#include "netstate.h"
#include "protocol.h"
#include "registry.h"
#include "screenshot.h"
#include "status.h"
#include "ui.h"
#include "wxstate.h"

/* ---- Global connection state ---- */
static redisContext *g_ctx = NULL;
static char g_host[128] = "127.0.0.1";
static int g_port = 6379;
static char g_auth[256] = {0};
static char g_self_version[80] = "unknown"; /* WI #369: shown on the self card */
#ifndef KPIDASH_TEST_STUBS
static evict_target_t g_evict[EVICT_TARGETS_MAX]; /* WI #374: pending card evicts */
static int g_evict_count = 0;
#endif

/* ---- Internal helpers ---- */

static bool do_connect(void) {
    struct timeval tv = {1, 0}; /* 1s connect timeout */
    redisContext *ctx = redisConnectWithTimeout(g_host, g_port, tv);
    if (!ctx || ctx->err) {
        if (ctx)
            redisFree(ctx);
        return false;
    }

    /* 50ms read timeout — fast failure without false-positive timeouts on a loaded Pi */
    struct timeval poll_tv = {0, 50000};
    if (redisSetTimeout(ctx, poll_tv) != REDIS_OK) {
        redisFree(ctx);
        return false;
    }

    if (g_auth[0]) {
        redisReply *r = redisCommand(ctx, "AUTH %s", g_auth);
        bool ok = r && r->type != REDIS_REPLY_ERROR;
        if (r)
            freeReplyObject(r);
        if (!ok) {
            redisFree(ctx);
            return false;
        }
    }

    g_ctx = ctx;
    return true;
}

/* ---- Public API ---- */

bool redis_connect(const char *host, int port, const char *auth) {
    strncpy(g_host, host ? host : "127.0.0.1", sizeof(g_host) - 1);
    g_port = port > 0 ? port : 6379;
    if (auth)
        strncpy(g_auth, auth, sizeof(g_auth) - 1);
    return do_connect();
}

void redis_disconnect(void) {
    if (g_ctx) {
        redisFree(g_ctx);
        g_ctx = NULL;
    }
}

bool redis_is_connected(void) {
    return g_ctx != NULL && g_ctx->err == 0;
}

bool redis_reconnect_if_needed(void) {
    if (g_ctx && g_ctx->err == 0)
        return true;
    if (g_ctx) {
        redisFree(g_ctx);
        g_ctx = NULL;
    }
    bool ok = do_connect();
    if (ok) {
        ui_hide_redis_error();
    } else {
        ui_show_redis_error("connection failed");
    }
    return ok;
}

/* ---- JSON parse helpers (exposed for unit tests) ---- */

bool redis_parse_health_json(const char *json, client_info_t *c) {
    cJSON *root = cJSON_Parse(json);
    if (!root)
        return false;

    cJSON *ts_obj = cJSON_GetObjectItemCaseSensitive(root, "last_seen_ts");
    cJSON *up_obj = cJSON_GetObjectItemCaseSensitive(root, "uptime_seconds");
    cJSON *os_obj = cJSON_GetObjectItemCaseSensitive(root, "os_name");
    cJSON *av_obj = cJSON_GetObjectItemCaseSensitive(root, "availability");

    if (cJSON_IsNumber(ts_obj))
        c->last_seen_ts = ts_obj->valuedouble;
    if (cJSON_IsNumber(up_obj))
        c->uptime_seconds = (float)up_obj->valuedouble;
    if (cJSON_IsString(os_obj))
        strncpy(c->os_name, os_obj->valuestring, OS_NAME_LEN - 1);
    /* Every payload decides it afresh; anything but the one known value is an
     * always-on host, so a typo renders RED rather than hiding an outage. */
    c->intermittent = cJSON_IsString(av_obj) && strcmp(av_obj->valuestring, "intermittent") == 0;

    cJSON_Delete(root);
    c->online = true;
    return true;
}

bool redis_parse_telemetry_json(const char *json, client_info_t *c) {
    cJSON *root = cJSON_Parse(json);
    if (!root)
        return false;

    cJSON *o;
#define GET_FLOAT(field, json_key)                        \
    o = cJSON_GetObjectItemCaseSensitive(root, json_key); \
    if (cJSON_IsNumber(o))                                \
        c->field = (float)o->valuedouble;
#define GET_UINT32(field, json_key)                       \
    o = cJSON_GetObjectItemCaseSensitive(root, json_key); \
    if (cJSON_IsNumber(o))                                \
        c->field = (uint32_t)o->valueint;
#define GET_DOUBLE(field, json_key)                       \
    o = cJSON_GetObjectItemCaseSensitive(root, json_key); \
    if (cJSON_IsNumber(o))                                \
        c->field = o->valuedouble;

    GET_FLOAT(cpu_pct, "cpu_pct")
    GET_FLOAT(top_core_pct, "top_core_pct")
    GET_UINT32(ram_used_mb, "ram_used_mb")
    GET_UINT32(ram_total_mb, "ram_total_mb")
    GET_DOUBLE(telemetry_ts, "ts")

#undef GET_FLOAT
#undef GET_UINT32
#undef GET_DOUBLE

    /* GPU */
    cJSON *gpu_obj = cJSON_GetObjectItemCaseSensitive(root, "gpu");
    if (cJSON_IsObject(gpu_obj)) {
        c->gpu.present = true;
        cJSON *gn = cJSON_GetObjectItemCaseSensitive(gpu_obj, "name");
        if (cJSON_IsString(gn))
            strncpy(c->gpu.name, gn->valuestring, GPU_NAME_LEN - 1);
        cJSON *vu = cJSON_GetObjectItemCaseSensitive(gpu_obj, "vram_used_mb");
        if (cJSON_IsNumber(vu))
            c->gpu.vram_used_mb = (uint32_t)vu->valueint;
        cJSON *vt = cJSON_GetObjectItemCaseSensitive(gpu_obj, "vram_total_mb");
        if (cJSON_IsNumber(vt))
            c->gpu.vram_total_mb = (uint32_t)vt->valueint;
        cJSON *cp = cJSON_GetObjectItemCaseSensitive(gpu_obj, "compute_pct");
        if (cJSON_IsNumber(cp))
            c->gpu.compute_pct = (float)cp->valuedouble;
    } else {
        c->gpu.present = false;
    }

    /* Disks */
    cJSON *disks_arr = cJSON_GetObjectItemCaseSensitive(root, "disks");
    c->disk_count = 0;
    if (cJSON_IsArray(disks_arr)) {
        cJSON *d;
        cJSON_ArrayForEach(d, disks_arr) {
            if (c->disk_count >= MAX_DISKS)
                break;
            disk_entry_t *de = &c->disks[c->disk_count];
            memset(de, 0, sizeof(*de));

            cJSON *lbl = cJSON_GetObjectItemCaseSensitive(d, "label");
            if (cJSON_IsString(lbl))
                strncpy(de->label, lbl->valuestring, LABEL_LEN - 1);

            cJSON *typ = cJSON_GetObjectItemCaseSensitive(d, "type");
            if (cJSON_IsString(typ)) {
                const char *ts = typ->valuestring;
                if (strcmp(ts, "nvme") == 0)
                    de->type = DISK_NVME;
                else if (strcmp(ts, "ssd") == 0)
                    de->type = DISK_SSD;
                else if (strcmp(ts, "hdd") == 0)
                    de->type = DISK_HDD;
                else
                    de->type = DISK_OTHER;
            }

            cJSON *ug = cJSON_GetObjectItemCaseSensitive(d, "used_gb");
            if (cJSON_IsNumber(ug))
                de->used_gb = (float)ug->valuedouble;
            cJSON *tg = cJSON_GetObjectItemCaseSensitive(d, "total_gb");
            if (cJSON_IsNumber(tg))
                de->total_gb = (float)tg->valuedouble;
            cJSON *pct = cJSON_GetObjectItemCaseSensitive(d, "pct");
            if (cJSON_IsNumber(pct))
                de->pct = (float)pct->valuedouble;

            c->disk_count++;
        }
    }

    cJSON_Delete(root);
    return true;
}

/* ---- Dev command state (T025) ---- */
static dev_cmd_state_t g_dev_cmd = {0};

const dev_cmd_state_t *redis_get_dev_cmd_state(void) {
    return &g_dev_cmd;
}

bool redis_parse_cmd_grid_json(const char *json, dev_cmd_state_t *state) {
    state->grid_enabled = false;
    state->grid_size = 0;
    state->grid_unit = false;
    state->grid_unit_size = 1.0f;
    if (!json)
        return true; /* absent key = disabled */
    cJSON *root = cJSON_Parse(json);
    if (!root)
        return false;
    cJSON *en = cJSON_GetObjectItemCaseSensitive(root, "enabled");
    cJSON *sz = cJSON_GetObjectItemCaseSensitive(root, "size");
    cJSON *unit = cJSON_GetObjectItemCaseSensitive(root, "unit");
    if (cJSON_IsBool(en))
        state->grid_enabled = cJSON_IsTrue(en);
    if (cJSON_IsBool(unit) && cJSON_IsTrue(unit)) {
        state->grid_unit = true;
        if (cJSON_IsNumber(sz) && sz->valuedouble > 0)
            state->grid_unit_size = (float)sz->valuedouble;
    } else {
        if (cJSON_IsNumber(sz) && sz->valueint > 0)
            state->grid_size = sz->valueint;
    }
    cJSON_Delete(root);
    return true;
}

bool redis_parse_cmd_textsize_json(const char *json, dev_cmd_state_t *state) {
    state->textsize_enabled = false;
    if (!json)
        return true;
    cJSON *root = cJSON_Parse(json);
    if (!root)
        return false;
    cJSON *en = cJSON_GetObjectItemCaseSensitive(root, "enabled");
    if (cJSON_IsBool(en))
        state->textsize_enabled = cJSON_IsTrue(en);
    cJSON_Delete(root);
    return true;
}

/* ---- Main poll cycle ---- */

void redis_poll(void) {
    if (!g_ctx || g_ctx->err)
        return;

    /* Step 1: Enumerate clients */
    redisReply *smr = redisCommand(g_ctx, "SMEMBERS " KPIDASH_KEY_CLIENTS);
    if (!smr || g_ctx->err) {
        if (smr)
            freeReplyObject(smr);
        redisFree(g_ctx);
        g_ctx = NULL;
        ui_show_redis_error("connection lost");
        return;
    }

    const char *hostnames[MAX_CLIENTS];
    int n_hosts = (int)smr->elements;
    if (n_hosts > MAX_CLIENTS)
        n_hosts = MAX_CLIENTS;
    for (int i = 0; i < n_hosts; i++) {
        hostnames[i] = smr->element[i]->str;
    }

    /* Step 2: Per-client health and telemetry */
    registry_lock();
    registry_remove_absent(hostnames, n_hosts);

    for (int i = 0; i < n_hosts; i++) {
        const char *h = hostnames[i];
        char key[256];

        /* Read both payloads BEFORE deciding whether this member gets a card
         * (WI #2524). kpidash:clients is append-only, so a member proves
         * nothing on its own; what admits a host is having published. */
        snprintf(key, sizeof(key), KPIDASH_KEY_HEALTH, h);
        redisReply *hr = redisCommand(g_ctx, "GET %s", key);
        snprintf(key, sizeof(key), KPIDASH_KEY_TELEMETRY, h);
        redisReply *tr = redisCommand(g_ctx, "GET %s", key);

        bool health_ok = hr && hr->type == REDIS_REPLY_STRING;
        bool telemetry_ok = tr && tr->type == REDIS_REPLY_STRING;

        client_info_t *c = registry_admit(h, health_ok || telemetry_ok);
        if (c) {
            if (health_ok) {
                redis_parse_health_json(hr->str, c);
            } else {
                c->online = false;
            }
            if (telemetry_ok) {
                redis_parse_telemetry_json(tr->str, c);
                /* WI #4523: the host columns sparklines. Recorded only when
                 * the key is present, so a host that stops reporting holds
                 * its last trace instead of drawing a drop to zero. */
                hosthist_record(c->hostname, (double)time(NULL), c->cpu_pct, c->top_core_pct,
                                c->gpu.present ? c->gpu.compute_pct : 0.0f);
            }
        }

        if (hr)
            freeReplyObject(hr);
        if (tr)
            freeReplyObject(tr);
    }
    registry_unlock();

    /* Step 3: Status ack check */
    status_redis_check_ack(g_ctx);

    /* Skip the rest if the context errored during Steps 2-3 */
    if (g_ctx->err) {
        freeReplyObject(smr);
        redisFree(g_ctx);
        g_ctx = NULL;
        ui_show_redis_error("connection lost during poll");
        return;
    }

    /* Step 4: Dev commands — grid and textsize */
    redisReply *gr = redisCommand(g_ctx, "GET " KPIDASH_KEY_CMD_GRID);
    dev_cmd_state_t new_cmd = {0};
    redis_parse_cmd_grid_json((gr && gr->type == REDIS_REPLY_STRING) ? gr->str : NULL, &new_cmd);
    if (gr)
        freeReplyObject(gr);

    redisReply *tsr = redisCommand(g_ctx, "GET " KPIDASH_KEY_CMD_TEXTSIZE);
    redis_parse_cmd_textsize_json((tsr && tsr->type == REDIS_REPLY_STRING) ? tsr->str : NULL,
                                  &new_cmd);
    if (tsr)
        freeReplyObject(tsr);

    g_dev_cmd = new_cmd;

    /* Step 6: Service status (sprint 006 / FR-022) */
    redis_poll_services();
    /* Apt-Temps per-zone cards (WI #364) */
    redis_poll_apttemps();
    /* Host staleness cards (WI #1903) */
    redis_poll_stale();
    /* Update-issue cards (WI #3642) */
    redis_poll_update();
    /* NAS big panel (WI #3956) */
    redis_poll_nas();
    /* Weather filler (WI #4576) */
    redis_poll_weather();
    /* Claude limits card (WI #4609) */
    redis_poll_limits();
    /* korg + klams card (WI #4613) */
    redis_poll_korg_klams();
    /* Card eviction command (WI #374) */
    redis_poll_evict();
    /* Network-health card (WI #4062) */
    redis_poll_net();
    /* Self service card (WI #369) */
    redis_publish_self_service();

    /* Step 7: One-shot device self-screenshot. Consume kpidash:screenshot
     * (GETDEL) and snapshot the active screen to BMP. A value starting with
     * '/' names the output path; anything else uses the default. Runs on the
     * UI thread — safe for lv_snapshot_take. */
    if (g_ctx && !g_ctx->err) {
        redisReply *ssr = redisCommand(g_ctx, "GETDEL " KPIDASH_KEY_SCREENSHOT);
        if (ssr && ssr->type == REDIS_REPLY_STRING && ssr->len > 0)
            screenshot_save(ssr->str[0] == '/' ? ssr->str : NULL);
        if (ssr)
            freeReplyObject(ssr);
    }

    freeReplyObject(smr);
}

/* ---- Round-trip diagnostic ---- */

bool redis_roundtrip_check(void) {
    if (!g_ctx || g_ctx->err) {
        printf("ERROR: Round trip to redis failed (no connection)\n");
        return false;
    }
    redisReply *wr = redisCommand(g_ctx, "SET kpidash:system:ping 1 EX 10");
    bool write_ok = wr && wr->type == REDIS_REPLY_STATUS;
    if (!write_ok) {
        if (wr && wr->type == REDIS_REPLY_ERROR)
            printf("ERROR: Round trip to redis failed (SET rejected: %s)\n", wr->str);
        else if (g_ctx->err)
            printf("ERROR: Round trip to redis failed (SET failed: %s)\n", g_ctx->errstr);
        else
            printf("ERROR: Round trip to redis failed (SET failed)\n");
        if (wr)
            freeReplyObject(wr);
        return false;
    }
    if (wr)
        freeReplyObject(wr);
    redisReply *rr = redisCommand(g_ctx, "GET kpidash:system:ping");
    bool read_ok = rr && rr->type == REDIS_REPLY_STRING && strcmp(rr->str, "1") == 0;
    if (rr)
        freeReplyObject(rr);
    if (read_ok) {
        printf("INFO: Round trip to redis success\n");
    } else {
        printf("ERROR: Round trip to redis failed (GET mismatch)\n");
    }
    return read_ok;
}

/* ---- System info ---- */

void redis_write_system_info(const char *logpath, const char *version) {
    if (!g_ctx || g_ctx->err)
        return;
    redisReply *r;
    r = redisCommand(g_ctx, "SET " KPIDASH_KEY_LOGPATH " %s", logpath);
    if (r)
        freeReplyObject(r);
    r = redisCommand(g_ctx, "SET " KPIDASH_KEY_VERSION " %s", version);
    if (r)
        freeReplyObject(r);
    strncpy(g_self_version, version ? version : "unknown", sizeof(g_self_version) - 1);
    g_self_version[sizeof(g_self_version) - 1] = '\0';
}

/* WI #369: the dashboard self-publishes a service card so the board shows its
 * own liveness + running version. Throttled to stay fresh without writing every
 * 1s poll. Key: kpidash:services:rpidash:<hostname>. */
void redis_publish_self_service(void) {
    if (!g_ctx || g_ctx->err)
        return;
    static double last = 0.0;
    double now = (double)time(NULL);
    if (now - last < 15.0)
        return;
    last = now;

    char host[64];
    if (gethostname(host, sizeof(host)) != 0)
        strncpy(host, "dashboard", sizeof(host) - 1);
    host[sizeof(host) - 1] = '\0';
    char *dot = strchr(host, '.');
    if (dot)
        *dot = '\0';

    cJSON *o = cJSON_CreateObject();
    if (!o)
        return;
    cJSON_AddNumberToObject(o, "ts", now);
    cJSON_AddStringToObject(o, "state", "ok");
    cJSON_AddStringToObject(o, "text", g_self_version);
    cJSON_AddStringToObject(o, "host", host);
    cJSON_AddNumberToObject(o, "icon", 20); /* icons.c index 20 = monitor_dashboard (WI #375) */
    char *json = cJSON_PrintUnformatted(o);
    cJSON_Delete(o);
    if (!json)
        return;

    redisReply *r = redisCommand(g_ctx, "SET kpidash:services:rpidash:%s %s", host, json);
    if (r)
        freeReplyObject(r);
    free(json);
}

/* ---- Memory telemetry (spec 005) ---- */

void redis_write_mem_sample(const mem_sample_t *s) {
    if (!g_ctx || g_ctx->err || !s)
        return;

    cJSON *root = cJSON_CreateObject();
    if (!root)
        return;
    cJSON_AddNumberToObject(root, "t", (double)s->t);
    cJSON_AddNumberToObject(root, "uptime_s", (double)s->uptime_s);
    cJSON_AddNumberToObject(root, "rss_bytes", (double)s->rss_bytes);
    cJSON_AddNumberToObject(root, "vsize_bytes", (double)s->vsize_bytes);
    cJSON_AddNumberToObject(root, "lvgl_total", (double)s->lvgl_total);
    cJSON_AddNumberToObject(root, "lvgl_free", (double)s->lvgl_free);
    cJSON_AddNumberToObject(root, "lvgl_used", (double)s->lvgl_used);
    cJSON_AddNumberToObject(root, "lvgl_max_used", (double)s->lvgl_max_used);
    cJSON_AddNumberToObject(root, "lvgl_frag_pct", (double)s->lvgl_frag_pct);
    cJSON_AddNumberToObject(root, "lvgl_free_biggest", (double)s->lvgl_free_biggest);

    char *json = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!json)
        return;

    redisReply *r;
    r = redisCommand(g_ctx, "SET " KPIDASH_KEY_SYSTEM_MEM_CURRENT " %s", json);
    if (r)
        freeReplyObject(r);
    r = redisCommand(g_ctx, "LPUSH " KPIDASH_KEY_SYSTEM_MEM_RING " %s", json);
    if (r)
        freeReplyObject(r);
    r = redisCommand(g_ctx, "LTRIM " KPIDASH_KEY_SYSTEM_MEM_RING " 0 %d", KPIDASH_MEM_RING_MAX - 1);
    if (r)
        freeReplyObject(r);

    free(json);
}

/* ============================================================
 * Sprint 006: service status polling (T021, T023)
 * ============================================================ */

int redis_parse_service_payload(const char *json, service_entry_t *out) {
    if (!json || !out) return -1;
    cJSON *root = cJSON_Parse(json);
    if (!root) return -1;

    cJSON *ts    = cJSON_GetObjectItemCaseSensitive(root, "ts");
    cJSON *state = cJSON_GetObjectItemCaseSensitive(root, "state");
    cJSON *text  = cJSON_GetObjectItemCaseSensitive(root, "text");
    cJSON *host  = cJSON_GetObjectItemCaseSensitive(root, "host");
    cJSON *icon  = cJSON_GetObjectItemCaseSensitive(root, "icon");

    if (!cJSON_IsNumber(ts) || !cJSON_IsString(state) || !cJSON_IsString(text)) {
        cJSON_Delete(root);
        return -1;
    }
    service_state_t st = service_parse_state(state->valuestring);
    if (st == SERVICE_STATE_UNKNOWN) {
        cJSON_Delete(root);
        return -1;
    }

    memset(out, 0, sizeof(*out));
    out->last_payload_ts = ts->valuedouble;
    out->last_valid_state = st;
    strncpy(out->text, text->valuestring, sizeof(out->text) - 1);
    if (cJSON_IsString(host)) {
        strncpy(out->host, host->valuestring, sizeof(out->host) - 1);
    }
    out->icon_index = cJSON_IsNumber(icon) ? (int)icon->valuedouble : -1;

    cJSON_Delete(root);
    return 0;
}

#ifndef KPIDASH_TEST_STUBS
void redis_poll_services(void) {
    if (!g_ctx || g_ctx->err) return;

    /* SCAN MATCH kpidash:services:* — paginate via cursor until 0. */
    char cursor[32] = "0";
    do {
        redisReply *sr = redisCommand(g_ctx, "SCAN %s MATCH %s COUNT 100",
                                      cursor, KPIDASH_KEY_SERVICES_PATTERN);
        if (!sr || sr->type != REDIS_REPLY_ARRAY || sr->elements != 2) {
            if (sr) freeReplyObject(sr);
            return;
        }
        if (sr->element[0]->type == REDIS_REPLY_STRING) {
            strncpy(cursor, sr->element[0]->str, sizeof(cursor) - 1);
            cursor[sizeof(cursor) - 1] = '\0';
        }
        redisReply *keys = sr->element[1];
        if (keys && keys->type == REDIS_REPLY_ARRAY) {
            for (size_t i = 0; i < keys->elements; i++) {
                const char *key = keys->element[i]->str;
                if (!key) continue;
                /* Split "kpidash:services:<name>:<host>" into name and host.
                 * Reject any key that doesn't have exactly two segments
                 * after the prefix (sprint 007 schema; AC-4). */
                const char *after = key + strlen(KPIDASH_KEY_SERVICES_PREFIX);
                const char *sep = strchr(after, ':');
                if (!sep || sep == after || !*(sep + 1)) continue;
                if (strchr(sep + 1, ':')) continue;
                size_t name_len = (size_t)(sep - after);
                char name[64];
                char host[64];
                if (name_len >= sizeof(name)) continue;
                memcpy(name, after, name_len);
                name[name_len] = '\0';
                strncpy(host, sep + 1, sizeof(host) - 1);
                host[sizeof(host) - 1] = '\0';

                redisReply *gr = redisCommand(g_ctx, "GET %s", key);
                if (gr && gr->type == REDIS_REPLY_STRING) {
                    service_entry_t parsed;
                    if (redis_parse_service_payload(gr->str, &parsed) == 0) {
                        service_entry_t *e = service_registry_find_or_create(name, host);
                        if (e) service_registry_apply_payload(e, &parsed);
                    }
                    /* parse failure: FR-022a — ignore silently. */
                }
                if (gr) freeReplyObject(gr);
            }
        }
        freeReplyObject(sr);
    } while (strcmp(cursor, "0") != 0);
}
#else
void redis_poll_services(void) { /* test stub */ }
#endif

/* ============================================================
 * WI #364: Apt-Temps per-zone card polling
 * ============================================================ */

int redis_parse_apttemps_payload(const char *json, apttemps_entry_t *out) {
    if (!json || !out) return -1;
    cJSON *root = cJSON_Parse(json);
    if (!root) return -1;

    cJSON *ts   = cJSON_GetObjectItemCaseSensitive(root, "ts");
    cJSON *tf   = cJSON_GetObjectItemCaseSensitive(root, "temp_f");
    cJSON *hum  = cJSON_GetObjectItemCaseSensitive(root, "humidity_pct");
    cJSON *zone = cJSON_GetObjectItemCaseSensitive(root, "zone");

    if (!cJSON_IsNumber(ts) || !cJSON_IsNumber(tf) || !cJSON_IsNumber(hum)) {
        cJSON_Delete(root);
        return -1;
    }

    memset(out, 0, sizeof(*out));
    out->last_payload_ts = ts->valuedouble;
    out->temp_f = (float)tf->valuedouble;
    out->humidity_pct = (int)hum->valuedouble;
    out->valid = true;
    if (cJSON_IsString(zone) && zone->valuestring) {
        strncpy(out->zone, zone->valuestring, sizeof(out->zone) - 1);
    }

    cJSON_Delete(root);
    return 0;
}

#ifndef KPIDASH_TEST_STUBS
void redis_poll_apttemps(void) {
    if (!g_ctx || g_ctx->err) return;

    char cursor[32] = "0";
    do {
        redisReply *sr = redisCommand(g_ctx, "SCAN %s MATCH %s COUNT 100",
                                      cursor, KPIDASH_KEY_APTTEMPS_PATTERN);
        if (!sr || sr->type != REDIS_REPLY_ARRAY || sr->elements != 2) {
            if (sr) freeReplyObject(sr);
            return;
        }
        if (sr->element[0]->type == REDIS_REPLY_STRING) {
            strncpy(cursor, sr->element[0]->str, sizeof(cursor) - 1);
            cursor[sizeof(cursor) - 1] = '\0';
        }
        redisReply *keys = sr->element[1];
        if (keys && keys->type == REDIS_REPLY_ARRAY) {
            for (size_t i = 0; i < keys->elements; i++) {
                const char *key = keys->element[i]->str;
                if (!key) continue;
                /* kpidash:apttemps:<slug> — exactly one segment after prefix. */
                const char *after = key + strlen(KPIDASH_KEY_APTTEMPS_PREFIX);
                if (!*after || strchr(after, ':')) continue;
                char slug[64];
                strncpy(slug, after, sizeof(slug) - 1);
                slug[sizeof(slug) - 1] = '\0';

                redisReply *gr = redisCommand(g_ctx, "GET %s", key);
                if (gr && gr->type == REDIS_REPLY_STRING) {
                    apttemps_entry_t parsed;
                    if (redis_parse_apttemps_payload(gr->str, &parsed) == 0) {
                        apttemps_entry_t *e = apttemps_registry_find_or_create(slug);
                        if (e) apttemps_registry_apply_payload(e, &parsed);
                    }
                }
                if (gr) freeReplyObject(gr);
            }
        }
        freeReplyObject(sr);
    } while (strcmp(cursor, "0") != 0);
}
#else
void redis_poll_apttemps(void) { /* test stub */ }
#endif

/* ============================================================
 * WI #1903: host staleness cards (kdash:stale:<host>:<deployer>)
 * ============================================================
 *
 * This reader inverts the rule the two above it follow. redis_poll_services
 * and redis_poll_apttemps drop an unparseable record and move on, which is
 * right when a record is the only thing asserting its own existence. Here the
 * KEY asserts staleness and the payload merely describes it, so dropping an
 * unreadable record would erase the flag and render as all-clear — reporting
 * a three-week outage as healthy. Every failure path below therefore falls
 * back to "still stale, detail unknown", never to "clear" (kdashdata CD-18).
 */

int redis_parse_stale_key(const char *key, char *host, size_t host_n, char *deployer,
                          size_t deployer_n) {
    if (!key || !host || !deployer || host_n == 0 || deployer_n == 0) return -1;
    const size_t plen = strlen(KDASH_KEY_STALE_PREFIX);
    if (strncmp(key, KDASH_KEY_STALE_PREFIX, plen) != 0) return -1;

    const char *after = key + plen;
    const char *sep = strchr(after, ':');
    size_t host_len = sep ? (size_t)(sep - after) : strlen(after);
    /* No host means nothing to attribute the flag to — the only key shape
     * this reader cannot honour. */
    if (host_len == 0) return -1;
    if (host_len >= host_n) return -1; /* truncating would invent a hostname */

    const char *dep = STALE_DEPLOYER_UNKNOWN;
    if (sep) {
        const char *rest = sep + 1;
        /* Exactly one more segment is the contract; a missing or over-long
         * tail costs the deployer's name, not the host's flag. */
        if (*rest && !strchr(rest, ':')) dep = rest;
    }
    if (strlen(dep) >= deployer_n) return -1;

    memcpy(host, after, host_len);
    host[host_len] = '\0';
    strncpy(deployer, dep, deployer_n - 1);
    deployer[deployer_n - 1] = '\0';
    return 0;
}

int redis_parse_stale_payload(const char *json, double *since) {
    if (!json || !*json || !since) return -1;
    cJSON *root = cJSON_Parse(json);
    if (!root) return -1;
    if (!cJSON_IsObject(root)) {
        cJSON_Delete(root);
        return -1;
    }
    /* `since` is Unix seconds as a bare number. cJSON_IsNumber is false for a
     * quoted ISO-8601 timestamp, which is the shape a writer reaching for the
     * fleet's other clock would publish and which nothing upstream rejects. */
    cJSON *s = cJSON_GetObjectItemCaseSensitive(root, "since");
    if (!cJSON_IsNumber(s)) {
        cJSON_Delete(root);
        return -1;
    }
    *since = s->valuedouble;
    cJSON_Delete(root);
    return 0;
}

/* Fold one observed key into the open cycle. `payload` is the GET result, or
 * NULL when it could not be read at all. Returns 0 if the host was raised,
 * -1 if the key named no host to raise.
 *
 * Split out of redis_poll_stale so CD-18's inversion is reachable by the test
 * gate: the poll itself needs a live Redis, and "an unreadable record still
 * raises its host" is the one rule in this file that must never regress. */
int redis_stale_apply_record(const char *key, const char *payload) {
    char host[64];
    char deployer[64];
    if (redis_parse_stale_key(key, host, sizeof(host), deployer, sizeof(deployer)) != 0)
        return -1;

    double since = 0.0;
    bool since_known = (payload != NULL) && (redis_parse_stale_payload(payload, &since) == 0);

    /* Raised either way. An unreadable payload costs the `since`, not the flag. */
    stale_registry_observe(host, deployer, since, since_known);
    return 0;
}

#ifndef KPIDASH_TEST_STUBS
void redis_poll_stale(void) {
    /* No connection: do not open a cycle at all, so the cards stay as they
     * were rather than being cleared by our own inability to look. */
    if (!g_ctx || g_ctx->err) return;

    stale_registry_begin_cycle();
    char cursor[32] = "0";
    do {
        redisReply *sr = redisCommand(g_ctx, "SCAN %s MATCH %s COUNT 100",
                                      cursor, KDASH_KEY_STALE_PATTERN);
        if (!sr || sr->type != REDIS_REPLY_ARRAY || sr->elements != 2) {
            if (sr) freeReplyObject(sr);
            stale_registry_abort_cycle();
            return;
        }
        if (sr->element[0]->type == REDIS_REPLY_STRING) {
            strncpy(cursor, sr->element[0]->str, sizeof(cursor) - 1);
            cursor[sizeof(cursor) - 1] = '\0';
        }
        redisReply *keys = sr->element[1];
        if (keys && keys->type == REDIS_REPLY_ARRAY) {
            for (size_t i = 0; i < keys->elements; i++) {
                const char *key = keys->element[i]->str;
                if (!key) continue;

                redisReply *gr = redisCommand(g_ctx, "GET %s", key);
                if (!gr) {
                    /* Connection died mid-cycle: what we have is a partial
                     * view, and committing it would clear hosts nothing has
                     * cleared. */
                    freeReplyObject(sr);
                    stale_registry_abort_cycle();
                    return;
                }
                if (gr->type == REDIS_REPLY_NIL) {
                    /* Deleted between the SCAN and the GET — a deployer
                     * genuinely cleared it. This is the one skip that is
                     * correct, because absence really is the all-clear. */
                    freeReplyObject(gr);
                    continue;
                }
                /* Anything else that is not a readable string (an error
                 * reply, a wrong type) reaches apply_record as NULL and
                 * raises the host without a `since`. */
                redis_stale_apply_record(key, gr->type == REDIS_REPLY_STRING ? gr->str : NULL);
                freeReplyObject(gr);
            }
        }
        freeReplyObject(sr);
    } while (strcmp(cursor, "0") != 0);

    /* Only a scan that ran to cursor 0 is a complete view of the feed, and
     * only a complete view may take cards down. */
    stale_registry_commit_cycle();
}
#else
void redis_poll_stale(void) { /* test stub */ }
#endif

/* ============================================================
 * WI #3642: update-issue cards (kdash:update:<host>:<kind>)
 * ============================================================
 *
 * The staleness reader's rules, unchanged (CD-18): the KEY raises the host,
 * the payload only describes it, and every failure path falls back to
 * "flagged, detail unknown". An unknown {kind} is not a failure at all — the
 * vocabulary is open, and the card renders the `issue` line, not the kind.
 */

int redis_parse_update_key(const char *key, char *host, size_t host_n, char *kind,
                           size_t kind_n) {
    if (!key || !host || !kind || host_n == 0 || kind_n == 0) return -1;
    const size_t plen = strlen(KDASH_KEY_UPDATE_PREFIX);
    if (strncmp(key, KDASH_KEY_UPDATE_PREFIX, plen) != 0) return -1;

    const char *after = key + plen;
    const char *sep = strchr(after, ':');
    size_t host_len = sep ? (size_t)(sep - after) : strlen(after);
    if (host_len == 0) return -1;
    if (host_len >= host_n) return -1; /* truncating would invent a hostname */

    const char *k = UPDATE_KIND_UNKNOWN;
    if (sep) {
        const char *rest = sep + 1;
        if (*rest && !strchr(rest, ':')) k = rest;
    }
    if (strlen(k) >= kind_n) return -1;

    memcpy(host, after, host_len);
    host[host_len] = '\0';
    strncpy(kind, k, kind_n - 1);
    kind[kind_n - 1] = '\0';
    return 0;
}

int redis_parse_update_payload(const char *json, double *since, bool *since_known, char *issue,
                               size_t issue_n) {
    if (since_known) *since_known = false;
    if (issue && issue_n) issue[0] = '\0';
    if (!json || !*json || !since || !since_known || !issue || issue_n == 0) return -1;
    cJSON *root = cJSON_Parse(json);
    if (!root) return -1;
    if (!cJSON_IsObject(root)) {
        cJSON_Delete(root);
        return -1;
    }
    /* Each field is read on its own, so a bad `since` costs the ordering and
     * keeps the line, and a bad `issue` costs the line and keeps the order. */
    cJSON *s = cJSON_GetObjectItemCaseSensitive(root, "since");
    if (cJSON_IsNumber(s)) {
        *since = s->valuedouble;
        *since_known = true;
    }
    cJSON *line = cJSON_GetObjectItemCaseSensitive(root, "issue");
    if (cJSON_IsString(line) && line->valuestring && *line->valuestring) {
        strncpy(issue, line->valuestring, issue_n - 1);
        issue[issue_n - 1] = '\0';
    }
    cJSON_Delete(root);
    return 0;
}

int redis_update_apply_record(const char *key, const char *payload) {
    char host[64];
    char kind[64];
    if (redis_parse_update_key(key, host, sizeof(host), kind, sizeof(kind)) != 0) return -1;

    double since = 0.0;
    bool since_known = false;
    char issue[UPDATE_ISSUE_LEN];
    issue[0] = '\0';
    if (payload)
        redis_parse_update_payload(payload, &since, &since_known, issue, sizeof(issue));

    /* Raised either way. An unreadable payload costs the detail, not the flag. */
    update_registry_observe(host, kind, since, since_known, issue);
    return 0;
}

#ifndef KPIDASH_TEST_STUBS
void redis_poll_update(void) {
    if (!g_ctx || g_ctx->err) return;

    update_registry_begin_cycle();
    char cursor[32] = "0";
    do {
        redisReply *sr = redisCommand(g_ctx, "SCAN %s MATCH %s COUNT 100", cursor,
                                      KDASH_KEY_UPDATE_PATTERN);
        if (!sr || sr->type != REDIS_REPLY_ARRAY || sr->elements != 2) {
            if (sr) freeReplyObject(sr);
            update_registry_abort_cycle();
            return;
        }
        if (sr->element[0]->type == REDIS_REPLY_STRING) {
            strncpy(cursor, sr->element[0]->str, sizeof(cursor) - 1);
            cursor[sizeof(cursor) - 1] = '\0';
        }
        redisReply *keys = sr->element[1];
        if (keys && keys->type == REDIS_REPLY_ARRAY) {
            for (size_t i = 0; i < keys->elements; i++) {
                const char *key = keys->element[i]->str;
                if (!key) continue;
                redisReply *gr = redisCommand(g_ctx, "GET %s", key);
                if (!gr) {
                    freeReplyObject(sr);
                    update_registry_abort_cycle();
                    return;
                }
                if (gr->type == REDIS_REPLY_NIL) {
                    /* DELeted between SCAN and GET: a real all-clear. */
                    freeReplyObject(gr);
                    continue;
                }
                redis_update_apply_record(key, gr->type == REDIS_REPLY_STRING ? gr->str : NULL);
                freeReplyObject(gr);
            }
        }
        freeReplyObject(sr);
    } while (strcmp(cursor, "0") != 0);

    update_registry_commit_cycle();
}
#else
void redis_poll_update(void) { /* test stub */ }
#endif

/* ============================================================
 * WI #374: card eviction command (GETDEL a JSON array of targets)
 * ============================================================ */
#ifndef KPIDASH_TEST_STUBS
void redis_poll_evict(void) {
    if (!g_ctx || g_ctx->err) return;
    redisReply *r = redisCommand(g_ctx, "GETDEL " KPIDASH_KEY_CMD_SERVICES_EVICT);
    if (!r || r->type != REDIS_REPLY_STRING) {
        if (r) freeReplyObject(r);
        return;
    }
    cJSON *root = cJSON_Parse(r->str);
    freeReplyObject(r);
    if (!root) return;
    if (cJSON_IsArray(root)) {
        int n = cJSON_GetArraySize(root);
        for (int i = 0; i < n && g_evict_count < EVICT_TARGETS_MAX; i++) {
            cJSON *o = cJSON_GetArrayItem(root, i);
            if (!cJSON_IsObject(o)) continue;
            cJSON *kind = cJSON_GetObjectItemCaseSensitive(o, "kind");
            cJSON *name = cJSON_GetObjectItemCaseSensitive(o, "name");
            cJSON *host = cJSON_GetObjectItemCaseSensitive(o, "host");
            if (!cJSON_IsString(name) || !name->valuestring[0]) continue;
            evict_target_t *t = &g_evict[g_evict_count++];
            memset(t, 0, sizeof(*t));
            strncpy(t->kind,
                    (cJSON_IsString(kind) && kind->valuestring) ? kind->valuestring : "service",
                    sizeof(t->kind) - 1);
            strncpy(t->name, name->valuestring, sizeof(t->name) - 1);
            if (cJSON_IsString(host) && host->valuestring)
                strncpy(t->host, host->valuestring, sizeof(t->host) - 1);
        }
    }
    cJSON_Delete(root);
}

int redis_take_evict_targets(evict_target_t *out, int max) {
    int n = g_evict_count < max ? g_evict_count : max;
    for (int i = 0; i < n; i++) out[i] = g_evict[i];
    g_evict_count = 0;
    return n;
}
#else
void redis_poll_evict(void) { /* test stub */ }
int redis_take_evict_targets(evict_target_t *out, int max) {
    (void)out;
    (void)max;
    return 0;
}
#endif

/* ============================================================
 * WI #4062: network-health card (kdash:net:<host>, kdashdata CD-27)
 * ============================================================ */

#ifndef KPIDASH_TEST_STUBS
void redis_poll_net(void) {
    if (!g_ctx || g_ctx->err) return;
    int n = net_host_count();
    if (n <= 0) return;

    /* One MGET for both families: the records, then (WI #4143) each host's
     * ack key. The acks go to netstate first, so this poll's records are
     * stored with them applied. */
    char keys[2 * NET_HOSTS_MAX][sizeof(KDASH_KEY_NET_ACK_PREFIX) + HOSTNAME_LEN];
    const char *argv[2 * NET_HOSTS_MAX + 1];
    argv[0] = "MGET";
    for (int i = 0; i < n; i++) {
        snprintf(keys[i], sizeof(keys[i]), KDASH_KEY_NET_PREFIX "%s", net_host_at(i)->host);
        snprintf(keys[n + i], sizeof(keys[n + i]), KDASH_KEY_NET_ACK_PREFIX "%s",
                 net_host_at(i)->host);
        argv[i + 1] = keys[i];
        argv[n + i + 1] = keys[n + i];
    }
    redisReply *r = redisCommandArgv(g_ctx, 2 * n + 1, argv, NULL);
    if (!r || r->type != REDIS_REPLY_ARRAY || (int)r->elements != 2 * n) {
        if (r) freeReplyObject(r);
        return;
    }
    double now = (double)time(NULL);
    /* Copy the names first: net_observe takes the host by name. */
    char hosts[NET_HOSTS_MAX][HOSTNAME_LEN];
    for (int i = 0; i < n; i++)
        snprintf(hosts[i], sizeof(hosts[i]), "%s", net_host_at(i)->host);
    for (int i = 0; i < n; i++) {
        redisReply *a = r->element[n + i];
        net_observe_ack(hosts[i], (a && a->type == REDIS_REPLY_STRING) ? a->str : NULL);
        redisReply *e = r->element[i];
        net_observe(hosts[i], (e && e->type == REDIS_REPLY_STRING) ? e->str : NULL, now);
    }
    freeReplyObject(r);
}
#else
void redis_poll_net(void) { /* test stub */ }
#endif

/* WI #3956: one GET of kdash:nas:<KPIDASH_NAS_HOST>. NIL is "publisher gone"
 * and goes to nasstate as NULL; a failed command is not reported at all, so a
 * Redis hiccup never turns the panel stale. */
#ifndef KPIDASH_TEST_STUBS
void redis_poll_nas(void) {
    if (!g_ctx || g_ctx->err) return;
    redisReply *r = redisCommand(g_ctx, "GET " KDASH_KEY_NAS_PREFIX KPIDASH_NAS_HOST);
    if (!r) return;
    if (r->type == REDIS_REPLY_STRING)
        nas_observe(r->str, (double)time(NULL));
    else if (r->type == REDIS_REPLY_NIL)
        nas_observe(NULL, (double)time(NULL));
    freeReplyObject(r);
}
#else
void redis_poll_nas(void) { /* test stub */ }
#endif

/* WI #4576: one GET of kdash:weather:home. The key has no TTL and kweather
 * owns it; this only reads. An unchanged value (every poll between kweather's
 * ten-minute writes) costs one strcmp in wxstate, not a parse. A record the
 * contract would reject is logged once and the last good one kept. */
#ifndef KPIDASH_TEST_STUBS
void redis_poll_weather(void) {
    if (!g_ctx || g_ctx->err) return;
    redisReply *r = redisCommand(g_ctx, "GET " KDASH_KEY_WEATHER);
    if (!r) return;
    if (r->type == REDIS_REPLY_STRING && wx_observe(r->str) < 0)
        fprintf(stderr, "redis: " KDASH_KEY_WEATHER " is off-contract; keeping the last record\n");
    freeReplyObject(r);
}
#else
void redis_poll_weather(void) { /* test stub */ }
#endif

/* WI #4609: one HGETALL of claude:limits, read only — several hosts write it
 * and the panel never does. It lives on this same Redis: KDASH_CLAUDE_REDIS
 * and the central stem both answer rpi53:6379 (verified 2026-11-14), so no
 * second connection. An empty reply (key absent) and a failed command both
 * keep the last record; a record libkdash rejects is logged and skipped. */
#ifndef KPIDASH_TEST_STUBS
void redis_poll_limits(void) {
    if (!g_ctx || g_ctx->err) return;
    redisReply *r = redisCommand(g_ctx, "HGETALL " KDASH_KEY_CLAUDE_LIMITS);
    if (!r) return;
    if (r->type == REDIS_REPLY_ARRAY && r->elements >= 2) {
        enum { MAXF = 32 };
        const char *f[MAXF], *v[MAXF];
        int n = 0;
        for (size_t i = 0; i + 1 < r->elements && n < MAXF; i += 2) {
            const redisReply *k = r->element[i], *x = r->element[i + 1];
            if (k->type != REDIS_REPLY_STRING || x->type != REDIS_REPLY_STRING) continue;
            f[n] = k->str;
            v[n] = x->str;
            n++;
        }
        if (lim_observe(f, v, n) < 0)
            fprintf(stderr, "redis: " KDASH_KEY_CLAUDE_LIMITS " is off-contract; keeping the last record\n");
    }
    freeReplyObject(r);
}
#else
void redis_poll_limits(void) { /* test stub */ }
#endif

/* WI #4613: one GET each of kdash:korg:pulse and kdash:klams:status, read only
 * — korg's timer on kai and klams-monitor on kubs0 own them. Both live on this
 * same Redis (KDASH_KEY_* are central-stem keys). An unchanged value costs one
 * strcmp in kkstate, not a parse; an absent key or a failed command keeps the
 * last record; a record libkdash rejects is logged and skipped. */
#ifndef KPIDASH_TEST_STUBS
static void poll_status_key(const char *key, int (*observe)(const char *)) {
    redisReply *r = redisCommand(g_ctx, "GET %s", key);
    if (!r) return;
    if (r->type == REDIS_REPLY_STRING && observe(r->str) < 0)
        fprintf(stderr, "redis: %s is off-contract; keeping the last record\n", key);
    freeReplyObject(r);
}

void redis_poll_korg_klams(void) {
    if (!g_ctx || g_ctx->err) return;
    poll_status_key(KDASH_KEY_KORG_PULSE, kk_observe_korg);
    poll_status_key(KDASH_KEY_KLAMS_STATUS, kk_observe_klams);
}
#else
void redis_poll_korg_klams(void) { /* test stub */ }
#endif
