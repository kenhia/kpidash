#include "redis.h"
#include "protocol.h"
#include "registry.h"
#include "status.h"
#include "fortune.h"
#include "ui.h"
#include <hiredis/hiredis.h>
#include <cjson/cJSON.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---- Global connection state ---- */
static redisContext *g_ctx = NULL;
static char g_host[128] = "127.0.0.1";
static int  g_port = 6379;
static char g_auth[256] = {0};

/* ---- Internal helpers ---- */

static bool do_connect(void) {
    struct timeval tv = {1, 0};  /* 1s connect timeout */
    redisContext *ctx = redisConnectWithTimeout(g_host, g_port, tv);
    if (!ctx || ctx->err) {
        if (ctx) redisFree(ctx);
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
        if (r) freeReplyObject(r);
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
    if (auth) strncpy(g_auth, auth, sizeof(g_auth) - 1);
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
    if (g_ctx && g_ctx->err == 0) return true;
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
    if (!root) return false;

    cJSON *ts_obj = cJSON_GetObjectItemCaseSensitive(root, "last_seen_ts");
    cJSON *up_obj = cJSON_GetObjectItemCaseSensitive(root, "uptime_seconds");

    if (cJSON_IsNumber(ts_obj)) c->last_seen_ts = ts_obj->valuedouble;
    if (cJSON_IsNumber(up_obj)) c->uptime_seconds = (float)up_obj->valuedouble;

    cJSON_Delete(root);
    c->online = true;
    return true;
}

bool redis_parse_telemetry_json(const char *json, client_info_t *c) {
    cJSON *root = cJSON_Parse(json);
    if (!root) return false;

    cJSON *o;
#define GET_FLOAT(field, json_key) \
    o = cJSON_GetObjectItemCaseSensitive(root, json_key); \
    if (cJSON_IsNumber(o)) c->field = (float)o->valuedouble;
#define GET_UINT32(field, json_key) \
    o = cJSON_GetObjectItemCaseSensitive(root, json_key); \
    if (cJSON_IsNumber(o)) c->field = (uint32_t)o->valueint;
#define GET_DOUBLE(field, json_key) \
    o = cJSON_GetObjectItemCaseSensitive(root, json_key); \
    if (cJSON_IsNumber(o)) c->field = o->valuedouble;

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
        if (cJSON_IsNumber(vu)) c->gpu.vram_used_mb = (uint32_t)vu->valueint;
        cJSON *vt = cJSON_GetObjectItemCaseSensitive(gpu_obj, "vram_total_mb");
        if (cJSON_IsNumber(vt)) c->gpu.vram_total_mb = (uint32_t)vt->valueint;
        cJSON *cp = cJSON_GetObjectItemCaseSensitive(gpu_obj, "compute_pct");
        if (cJSON_IsNumber(cp)) c->gpu.compute_pct = (float)cp->valuedouble;
    } else {
        c->gpu.present = false;
    }

    /* Disks */
    cJSON *disks_arr = cJSON_GetObjectItemCaseSensitive(root, "disks");
    c->disk_count = 0;
    if (cJSON_IsArray(disks_arr)) {
        cJSON *d;
        cJSON_ArrayForEach(d, disks_arr) {
            if (c->disk_count >= MAX_DISKS) break;
            disk_entry_t *de = &c->disks[c->disk_count];
            memset(de, 0, sizeof(*de));

            cJSON *lbl = cJSON_GetObjectItemCaseSensitive(d, "label");
            if (cJSON_IsString(lbl))
                strncpy(de->label, lbl->valuestring, LABEL_LEN - 1);

            cJSON *typ = cJSON_GetObjectItemCaseSensitive(d, "type");
            if (cJSON_IsString(typ)) {
                const char *ts = typ->valuestring;
                if (strcmp(ts, "nvme") == 0)      de->type = DISK_NVME;
                else if (strcmp(ts, "ssd") == 0)  de->type = DISK_SSD;
                else if (strcmp(ts, "hdd") == 0)  de->type = DISK_HDD;
                else                              de->type = DISK_OTHER;
            }

            cJSON *ug = cJSON_GetObjectItemCaseSensitive(d, "used_gb");
            if (cJSON_IsNumber(ug)) de->used_gb = (float)ug->valuedouble;
            cJSON *tg = cJSON_GetObjectItemCaseSensitive(d, "total_gb");
            if (cJSON_IsNumber(tg)) de->total_gb = (float)tg->valuedouble;
            cJSON *pct = cJSON_GetObjectItemCaseSensitive(d, "pct");
            if (cJSON_IsNumber(pct)) de->pct = (float)pct->valuedouble;

            c->disk_count++;
        }
    }

    cJSON_Delete(root);
    return true;
}

/* ---- Shared activity state ---- */
static activity_t g_activities[ACTIVITY_MAX_DISPLAY];
static int g_activity_count = 0;

const activity_t *redis_get_activities(int *count) {
    *count = g_activity_count;
    return g_activities;
}

/* ---- Shared repo state ---- */
#define MAX_REPO_ENTRIES 128
static repo_entry_t g_repos[MAX_REPO_ENTRIES];
static int g_repo_count = 0;

const repo_entry_t *redis_get_repos(int *count) {
    *count = g_repo_count;
    return g_repos;
}

/* ---- Poll helpers ---- */

static void poll_activities(void) {
    redisReply *zr = redisCommand(g_ctx, "ZREVRANGE " KPIDASH_KEY_ACTIVITIES " 0 %d",
                                  ACTIVITY_MAX_DISPLAY - 1);
    if (!zr || zr->type != REDIS_REPLY_ARRAY) {
        if (zr) freeReplyObject(zr);
        g_activity_count = 0;
        return;
    }

    int count = 0;
    for (size_t i = 0; i < zr->elements && count < ACTIVITY_MAX_DISPLAY; i++) {
        const char *id = zr->element[i]->str;
        char key[256];
        snprintf(key, sizeof(key), KPIDASH_KEY_ACTIVITY, id);
        redisReply *hr = redisCommand(g_ctx, "HGETALL %s", key);
        if (!hr || hr->type != REDIS_REPLY_ARRAY) {
            if (hr) freeReplyObject(hr);
            continue;
        }

        activity_t *a = &g_activities[count];
        memset(a, 0, sizeof(*a));
        strncpy(a->activity_id, id, ACTIVITY_ID_LEN - 1);

        for (size_t j = 0; j + 1 < hr->elements; j += 2) {
            const char *k = hr->element[j]->str;
            const char *v = hr->element[j + 1]->str;
            if (!k || !v) continue;
            if (strcmp(k, "host") == 0)  strncpy(a->host, v, HOSTNAME_LEN - 1);
            else if (strcmp(k, "name") == 0) strncpy(a->name, v, ACTIVITY_NAME_LEN - 1);
            else if (strcmp(k, "state") == 0) a->is_done = (strcmp(v, "done") == 0);
            else if (strcmp(k, "start_ts") == 0) a->start_ts = atof(v);
            else if (strcmp(k, "end_ts") == 0)   a->end_ts   = atof(v);
        }
        freeReplyObject(hr);
        count++;
    }
    g_activity_count = count;
    freeReplyObject(zr);
}

static void poll_repos(const char **hostnames, int n_hosts) {
    g_repo_count = 0;
    for (int i = 0; i < n_hosts && g_repo_count < MAX_REPO_ENTRIES; i++) {
        char key[256];
        snprintf(key, sizeof(key), KPIDASH_KEY_REPOS, hostnames[i]);
        redisReply *hr = redisCommand(g_ctx, "HGETALL %s", key);
        if (!hr || hr->type != REDIS_REPLY_ARRAY || hr->elements == 0) {
            if (hr) freeReplyObject(hr);
            continue;
        }
        /* Each field is a path, each value is JSON */
        for (size_t j = 0; j + 1 < hr->elements; j += 2) {
            if (g_repo_count >= MAX_REPO_ENTRIES) break;
            const char *path = hr->element[j]->str;
            const char *json = hr->element[j + 1]->str;
            if (!path || !json) continue;

            cJSON *root = cJSON_Parse(json);
            if (!root) continue;

            repo_entry_t *re = &g_repos[g_repo_count];
            memset(re, 0, sizeof(*re));
            strncpy(re->host, hostnames[i], HOSTNAME_LEN - 1);
            strncpy(re->path, path, sizeof(re->path) - 1);

            cJSON *nm = cJSON_GetObjectItemCaseSensitive(root, "name");
            if (cJSON_IsString(nm)) strncpy(re->name, nm->valuestring, LABEL_LEN - 1);
            cJSON *br = cJSON_GetObjectItemCaseSensitive(root, "branch");
            if (cJSON_IsString(br)) strncpy(re->branch, br->valuestring, sizeof(re->branch) - 1);
            cJSON *di = cJSON_GetObjectItemCaseSensitive(root, "is_dirty");
            if (cJSON_IsBool(di)) re->is_dirty = cJSON_IsTrue(di);
            cJSON *ex = cJSON_GetObjectItemCaseSensitive(root, "explicit");
            re->sort_order = (cJSON_IsBool(ex) && cJSON_IsTrue(ex)) ? 0 : 1;
            cJSON *ts = cJSON_GetObjectItemCaseSensitive(root, "ts");
            if (cJSON_IsNumber(ts)) re->ts = ts->valuedouble;

            cJSON_Delete(root);
            g_repo_count++;
        }
        freeReplyObject(hr);
    }

    /* Sort: explicit repos (sort_order=0) first, then by name */
    int repo_cmp(const void *a, const void *b) {
        const repo_entry_t *ra = (const repo_entry_t *)a;
        const repo_entry_t *rb = (const repo_entry_t *)b;
        if (ra->sort_order != rb->sort_order)
            return ra->sort_order - rb->sort_order;
        return strncmp(ra->name, rb->name, LABEL_LEN);
    }
    qsort(g_repos, (size_t)g_repo_count, sizeof(repo_entry_t), repo_cmp);
}

/* ---- Main poll cycle ---- */

void redis_poll(void) {
    if (!g_ctx || g_ctx->err) return;

    /* Step 1: Enumerate clients */
    redisReply *smr = redisCommand(g_ctx, "SMEMBERS " KPIDASH_KEY_CLIENTS);
    if (!smr || g_ctx->err) {
        if (smr) freeReplyObject(smr);
        redisFree(g_ctx);
        g_ctx = NULL;
        ui_show_redis_error("connection lost");
        return;
    }

    const char *hostnames[MAX_CLIENTS];
    int n_hosts = (int)smr->elements;
    if (n_hosts > MAX_CLIENTS) n_hosts = MAX_CLIENTS;
    for (int i = 0; i < n_hosts; i++) {
        hostnames[i] = smr->element[i]->str;
    }

    /* Step 2: Per-client health and telemetry */
    registry_lock();
    registry_remove_absent(hostnames, n_hosts);

    for (int i = 0; i < n_hosts; i++) {
        const char *h = hostnames[i];
        client_info_t *c = registry_find_or_create(h);
        if (!c) continue;

        /* Health */
        char key[256];
        snprintf(key, sizeof(key), KPIDASH_KEY_HEALTH, h);
        redisReply *hr = redisCommand(g_ctx, "GET %s", key);
        if (hr && hr->type == REDIS_REPLY_STRING) {
            redis_parse_health_json(hr->str, c);
        } else {
            c->online = false;
        }
        if (hr) freeReplyObject(hr);

        /* Telemetry */
        snprintf(key, sizeof(key), KPIDASH_KEY_TELEMETRY, h);
        redisReply *tr = redisCommand(g_ctx, "GET %s", key);
        if (tr && tr->type == REDIS_REPLY_STRING) {
            redis_parse_telemetry_json(tr->str, c);
        }
        if (tr) freeReplyObject(tr);
    }
    registry_unlock();

    /* Step 3: Activities */
    poll_activities();

    /* Step 4: Repo status */
    poll_repos(hostnames, n_hosts);

    /* Step 5: Status ack check */
    status_redis_check_ack(g_ctx);

    /* Step 6: Fortune pushed check — skip if context errored during Steps 2-5 */
    if (g_ctx->err) {
        freeReplyObject(smr);
        redisFree(g_ctx);
        g_ctx = NULL;
        ui_show_redis_error("connection lost during poll");
        return;
    }
    redisReply *fr = redisCommand(g_ctx, "EXISTS " KPIDASH_KEY_FORTUNE_PUSHED);
    if (fr && fr->type == REDIS_REPLY_INTEGER && fr->integer == 1) {
        freeReplyObject(fr);
        fr = redisCommand(g_ctx, "GET " KPIDASH_KEY_FORTUNE_PUSHED);
        if (fr && fr->type == REDIS_REPLY_STRING) {
            fortune_on_pushed(fr->str);
        }
    }
    if (fr) freeReplyObject(fr);

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
        if (wr) freeReplyObject(wr);
        return false;
    }
    if (wr) freeReplyObject(wr);
    redisReply *rr = redisCommand(g_ctx, "GET kpidash:system:ping");
    bool read_ok = rr && rr->type == REDIS_REPLY_STRING && strcmp(rr->str, "1") == 0;
    if (rr) freeReplyObject(rr);
    if (read_ok) {
        printf("INFO: Round trip to redis success\n");
    } else {
        printf("ERROR: Round trip to redis failed (GET mismatch)\n");
    }
    return read_ok;
}

/* ---- System info ---- */

void redis_write_system_info(const char *logpath, const char *version) {
    if (!g_ctx || g_ctx->err) return;
    redisReply *r;
    r = redisCommand(g_ctx, "SET " KPIDASH_KEY_LOGPATH " %s", logpath);
    if (r) freeReplyObject(r);
    r = redisCommand(g_ctx, "SET " KPIDASH_KEY_VERSION " %s", version);
    if (r) freeReplyObject(r);
}

/* ---- Fortune ---- */

void redis_write_fortune_current(const char *json) {
    if (!g_ctx || g_ctx->err) return;
    redisReply *r = redisCommand(g_ctx, "SET " KPIDASH_KEY_FORTUNE_CURRENT " %s", json);
    if (r) freeReplyObject(r);
}
