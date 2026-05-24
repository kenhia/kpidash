#include "redis.h"

#include <cjson/cJSON.h>
#include <hiredis/hiredis.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "fortune.h"
#include "memstat.h"
#include "protocol.h"
#include "registry.h"
#include "status.h"
#include "ui.h"

/* ---- Global connection state ---- */
static redisContext *g_ctx = NULL;
static char g_host[128] = "127.0.0.1";
static int g_port = 6379;
static char g_auth[256] = {0};

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

    if (cJSON_IsNumber(ts_obj))
        c->last_seen_ts = ts_obj->valuedouble;
    if (cJSON_IsNumber(up_obj))
        c->uptime_seconds = (float)up_obj->valuedouble;
    if (cJSON_IsString(os_obj))
        strncpy(c->os_name, os_obj->valuestring, OS_NAME_LEN - 1);

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

/* ---- Dev command state (T025) ---- */
static dev_cmd_state_t g_dev_cmd = {0};

const dev_cmd_state_t *redis_get_dev_cmd_state(void) {
    return &g_dev_cmd;
}

/* ---- Dev telemetry snapshot (Phase 6.1) ---- */
static dev_telemetry_t g_dev_telemetry = {0};

const dev_telemetry_t *redis_get_dev_telemetry(void) {
    return &g_dev_telemetry;
}

bool redis_parse_dev_telemetry_json(const char *json, dev_telemetry_t *dt) {
    memset(dt, 0, sizeof(*dt));
    if (!json)
        return true; /* absent key = invalid */
    cJSON *root = cJSON_Parse(json);
    if (!root)
        return false;

    dt->valid = true;

    cJSON *o;
    o = cJSON_GetObjectItemCaseSensitive(root, "cpu_pct");
    if (cJSON_IsNumber(o))
        dt->cpu_pct = (float)o->valuedouble;
    o = cJSON_GetObjectItemCaseSensitive(root, "top_core_pct");
    if (cJSON_IsNumber(o))
        dt->top_core_pct = (float)o->valuedouble;
    o = cJSON_GetObjectItemCaseSensitive(root, "ram_used_mb");
    if (cJSON_IsNumber(o))
        dt->ram_used_mb = (uint32_t)o->valueint;
    o = cJSON_GetObjectItemCaseSensitive(root, "ram_total_mb");
    if (cJSON_IsNumber(o))
        dt->ram_total_mb = (uint32_t)o->valueint;

    cJSON *gpu_obj = cJSON_GetObjectItemCaseSensitive(root, "gpu");
    if (cJSON_IsObject(gpu_obj)) {
        dt->gpu_present = true;
        o = cJSON_GetObjectItemCaseSensitive(gpu_obj, "compute_pct");
        if (cJSON_IsNumber(o))
            dt->gpu_compute_pct = (float)o->valuedouble;
        o = cJSON_GetObjectItemCaseSensitive(gpu_obj, "vram_used_mb");
        if (cJSON_IsNumber(o))
            dt->gpu_vram_used_mb = (uint32_t)o->valueint;
        o = cJSON_GetObjectItemCaseSensitive(gpu_obj, "vram_total_mb");
        if (cJSON_IsNumber(o))
            dt->gpu_vram_total_mb = (uint32_t)o->valueint;
    }

    /* Sprint 006 / T034: pick up source host; default to "(legacy)" sentinel
     * for unupgraded clients (FR-013 edge case). */
    o = cJSON_GetObjectItemCaseSensitive(root, "host");
    if (cJSON_IsString(o) && o->valuestring && o->valuestring[0]) {
        strncpy(dt->host, o->valuestring, sizeof(dt->host) - 1);
    } else {
        strncpy(dt->host, "(legacy)", sizeof(dt->host) - 1);
    }

    cJSON_Delete(root);
    return true;
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

bool redis_parse_cmd_graph_json(const char *json, dev_cmd_state_t *state) {
    state->graph_enabled = false;
    state->graph_client[0] = '\0';
    if (!json)
        return true;
    cJSON *root = cJSON_Parse(json);
    if (!root)
        return false;
    cJSON *en = cJSON_GetObjectItemCaseSensitive(root, "enabled");
    cJSON *cl = cJSON_GetObjectItemCaseSensitive(root, "client");
    if (cJSON_IsBool(en))
        state->graph_enabled = cJSON_IsTrue(en);
    if (cJSON_IsString(cl) && cl->valuestring) {
        strncpy(state->graph_client, cl->valuestring, HOSTNAME_LEN - 1);
        state->graph_client[HOSTNAME_LEN - 1] = '\0';
    }
    cJSON_Delete(root);
    return true;
}

/* ---- Poll helpers ---- */

static void poll_activities(void) {
    redisReply *zr =
        redisCommand(g_ctx, "ZREVRANGE " KPIDASH_KEY_ACTIVITIES " 0 %d", ACTIVITY_MAX_DISPLAY - 1);
    if (!zr || zr->type != REDIS_REPLY_ARRAY) {
        if (zr)
            freeReplyObject(zr);
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
            if (hr)
                freeReplyObject(hr);
            continue;
        }

        activity_t *a = &g_activities[count];
        memset(a, 0, sizeof(*a));
        strncpy(a->activity_id, id, ACTIVITY_ID_LEN - 1);

        for (size_t j = 0; j + 1 < hr->elements; j += 2) {
            const char *k = hr->element[j]->str;
            const char *v = hr->element[j + 1]->str;
            if (!k || !v)
                continue;
            if (strcmp(k, "host") == 0)
                strncpy(a->host, v, HOSTNAME_LEN - 1);
            else if (strcmp(k, "name") == 0)
                strncpy(a->name, v, ACTIVITY_NAME_LEN - 1);
            else if (strcmp(k, "state") == 0)
                a->is_done = (strcmp(v, "done") == 0);
            else if (strcmp(k, "start_ts") == 0)
                a->start_ts = atof(v);
            else if (strcmp(k, "end_ts") == 0)
                a->end_ts = atof(v);
        }
        freeReplyObject(hr);
        count++;
    }
    g_activity_count = count;
    freeReplyObject(zr);
}

bool redis_parse_repo_json(const char *json, repo_entry_t *re) {
    if (!json || !re)
        return false;
    cJSON *root = cJSON_Parse(json);
    if (!root)
        return false;

    cJSON *nm = cJSON_GetObjectItemCaseSensitive(root, "name");
    if (cJSON_IsString(nm))
        strncpy(re->name, nm->valuestring, LABEL_LEN - 1);
    cJSON *br = cJSON_GetObjectItemCaseSensitive(root, "branch");
    if (cJSON_IsString(br))
        strncpy(re->branch, br->valuestring, sizeof(re->branch) - 1);
    cJSON *db = cJSON_GetObjectItemCaseSensitive(root, "default_branch");
    if (cJSON_IsString(db))
        strncpy(re->default_branch, db->valuestring, sizeof(re->default_branch) - 1);
    cJSON *di = cJSON_GetObjectItemCaseSensitive(root, "is_dirty");
    if (cJSON_IsBool(di))
        re->is_dirty = cJSON_IsTrue(di);
    cJSON *dh = cJSON_GetObjectItemCaseSensitive(root, "detached_head");
    if (cJSON_IsBool(dh))
        re->detached_head = cJSON_IsTrue(dh);
    cJSON *ah = cJSON_GetObjectItemCaseSensitive(root, "ahead");
    if (cJSON_IsNumber(ah))
        re->ahead = ah->valueint;
    cJSON *bh = cJSON_GetObjectItemCaseSensitive(root, "behind");
    if (cJSON_IsNumber(bh))
        re->behind = bh->valueint;
    cJSON *uc = cJSON_GetObjectItemCaseSensitive(root, "untracked_count");
    if (cJSON_IsNumber(uc))
        re->untracked_count = uc->valueint;
    cJSON *cc = cJSON_GetObjectItemCaseSensitive(root, "changed_count");
    if (cJSON_IsNumber(cc))
        re->changed_count = cc->valueint;
    cJSON *dc = cJSON_GetObjectItemCaseSensitive(root, "deleted_count");
    if (cJSON_IsNumber(dc))
        re->deleted_count = dc->valueint;
    cJSON *rc = cJSON_GetObjectItemCaseSensitive(root, "renamed_count");
    if (cJSON_IsNumber(rc))
        re->renamed_count = rc->valueint;
    cJSON *lc = cJSON_GetObjectItemCaseSensitive(root, "last_commit_ts");
    if (cJSON_IsNumber(lc))
        re->last_commit_ts = lc->valuedouble;
    cJSON *ex = cJSON_GetObjectItemCaseSensitive(root, "explicit");
    re->sort_order = (cJSON_IsBool(ex) && cJSON_IsTrue(ex)) ? 0 : 1;
    cJSON *ts = cJSON_GetObjectItemCaseSensitive(root, "ts");
    if (cJSON_IsNumber(ts))
        re->ts = ts->valuedouble;

    cJSON_Delete(root);
    return true;
}

static void poll_repos(const char **hostnames, int n_hosts) {
    g_repo_count = 0;
    for (int i = 0; i < n_hosts && g_repo_count < MAX_REPO_ENTRIES; i++) {
        char key[256];
        snprintf(key, sizeof(key), KPIDASH_KEY_REPOS, hostnames[i]);
        redisReply *hr = redisCommand(g_ctx, "HGETALL %s", key);
        if (!hr || hr->type != REDIS_REPLY_ARRAY || hr->elements == 0) {
            if (hr)
                freeReplyObject(hr);
            continue;
        }
        /* Each field is a path, each value is JSON */
        for (size_t j = 0; j + 1 < hr->elements; j += 2) {
            if (g_repo_count >= MAX_REPO_ENTRIES)
                break;
            const char *path = hr->element[j]->str;
            const char *json = hr->element[j + 1]->str;
            if (!path || !json)
                continue;

            repo_entry_t *re = &g_repos[g_repo_count];
            memset(re, 0, sizeof(*re));
            strncpy(re->host, hostnames[i], HOSTNAME_LEN - 1);
            strncpy(re->path, path, sizeof(re->path) - 1);

            if (!redis_parse_repo_json(json, re))
                continue;
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
        client_info_t *c = registry_find_or_create(h);
        if (!c)
            continue;

        /* Health */
        char key[256];
        snprintf(key, sizeof(key), KPIDASH_KEY_HEALTH, h);
        redisReply *hr = redisCommand(g_ctx, "GET %s", key);
        if (hr && hr->type == REDIS_REPLY_STRING) {
            redis_parse_health_json(hr->str, c);
        } else {
            c->online = false;
        }
        if (hr)
            freeReplyObject(hr);

        /* Telemetry */
        snprintf(key, sizeof(key), KPIDASH_KEY_TELEMETRY, h);
        redisReply *tr = redisCommand(g_ctx, "GET %s", key);
        if (tr && tr->type == REDIS_REPLY_STRING) {
            redis_parse_telemetry_json(tr->str, c);
        }
        if (tr)
            freeReplyObject(tr);
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
    if (fr)
        freeReplyObject(fr);

    /* Step 7: Dev commands — grid, textsize, and graph */
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

    redisReply *gpr = redisCommand(g_ctx, "GET " KPIDASH_KEY_CMD_GRAPH);
    redis_parse_cmd_graph_json((gpr && gpr->type == REDIS_REPLY_STRING) ? gpr->str : NULL,
                               &new_cmd);
    if (gpr)
        freeReplyObject(gpr);

    g_dev_cmd = new_cmd;

    /* Step 8: Dev telemetry (sprint 006 / T031: always poll, no toggle gate).
     * Iterate all known clients and fetch their dev_telemetry key. On each
     * successful parse, register the host with the graph-host series, touch
     * its last_sample_ts, and store the telemetry on the series slot so the
     * UI (T035) can render one dev_graph per host directly from the snapshot.
     * g_dev_telemetry is kept for legacy code paths but is no longer the
     * source of truth for graph placement. */
    g_dev_telemetry.valid = false;
    bool have_non_legacy = false;
    for (int i = 0; i < n_hosts; i++) {
        char dt_key[256];
        snprintf(dt_key, sizeof(dt_key), KPIDASH_KEY_DEV_TELEMETRY, hostnames[i]);
        redisReply *dtr = redisCommand(g_ctx, "GET %s", dt_key);
        if (dtr && dtr->type == REDIS_REPLY_STRING) {
            dev_telemetry_t parsed = {0};
            if (redis_parse_dev_telemetry_json(dtr->str, &parsed) && parsed.valid) {
                graph_host_series_t *series = graph_host_find_or_create(parsed.host);
                if (series) {
                    series->telemetry = parsed;
                }
                graph_host_touch(parsed.host, (double)time(NULL));
                bool is_legacy = (strcmp(parsed.host, "(legacy)") == 0);
                if (!is_legacy && !have_non_legacy) {
                    have_non_legacy = true;
                    g_dev_telemetry = parsed;
                } else if (!have_non_legacy) {
                    g_dev_telemetry = parsed;
                }
            }
        }
        if (dtr)
            freeReplyObject(dtr);
    }

    /* Step 9: Service status (sprint 006 / FR-022) */
    redis_poll_services();

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
}

/* ---- Fortune ---- */

void redis_write_fortune_current(const char *json) {
    if (!g_ctx || g_ctx->err)
        return;
    redisReply *r = redisCommand(g_ctx, "SET " KPIDASH_KEY_FORTUNE_CURRENT " %s", json);
    if (r)
        freeReplyObject(r);
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
                const char *name = key + strlen(KPIDASH_KEY_SERVICES_PREFIX);
                if (!*name) continue;

                redisReply *gr = redisCommand(g_ctx, "GET %s", key);
                if (gr && gr->type == REDIS_REPLY_STRING) {
                    service_entry_t parsed;
                    if (redis_parse_service_payload(gr->str, &parsed) == 0) {
                        service_entry_t *e = service_registry_find_or_create(name);
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
