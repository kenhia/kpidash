/* render_nas_card.c — offline render of the NAS card to BMP (sprint 033).
 * Not a test: a review tool, built with the full native tree (it needs LVGL),
 * so the card can be compared with the mock-up without a Pi.
 *
 *   render_nas_card <out.bmp> [ok|warn|crit|stale]
 *
 * The data is gratch's own record (tests/fixtures/nas-gratch-2026-11-12.json),
 * and the four states are the mock-up's: `warn` gives sdc two pending
 * sectors, `crit` degrades the pool and fails sdd's SMART, `stale` reads the
 * record four minutes after the key went away. The canvas is the whole 2×1
 * cell (1264×628), the space the mock-up draws in. */
#include <cjson/cJSON.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bmp_write.h"
#include "layout.h"
#include "lvgl.h"
#include "nasstate.h"
#include "protocol.h"
#include "registry.h"
#include "widgets/nas_card.h"

static char *slurp(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f)
        return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *buf = malloc((size_t)n + 1);
    if (buf && fread(buf, 1, (size_t)n, f) != (size_t)n) {
        free(buf);
        buf = NULL;
    }
    if (buf)
        buf[n] = '\0';
    fclose(f);
    return buf;
}

static cJSON *disk(cJSON *root, const char *dev) {
    cJSON *d;
    cJSON_ArrayForEach(d, cJSON_GetObjectItemCaseSensitive(root, "disks")) {
        const cJSON *n = cJSON_GetObjectItemCaseSensitive(d, "device");
        if (cJSON_IsString(n) && strcmp(n->valuestring, dev) == 0)
            return d;
    }
    return NULL;
}

static void flush_cb(lv_display_t *d, const lv_area_t *a, uint8_t *px) {
    (void)a;
    (void)px;
    lv_display_flush_ready(d);
}

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "usage: %s out.bmp [ok|warn|crit|stale]\n", argv[0]);
        return 2;
    }
    const char *state = argc > 2 ? argv[2] : "ok";
    char *text = slurp(NAS_LIVE_FIXTURE);
    cJSON *root = text ? cJSON_Parse(text) : NULL;
    free(text);
    if (!root) {
        fprintf(stderr, "cannot read %s\n", NAS_LIVE_FIXTURE);
        return 1;
    }
    if (strcmp(state, "warn") == 0) {
        cJSON_ReplaceItemInObject(disk(root, "/dev/sdc"), "pending_sectors", cJSON_CreateNumber(2));
    } else if (strcmp(state, "crit") == 0) {
        cJSON *pool = cJSON_GetArrayItem(cJSON_GetObjectItemCaseSensitive(root, "pools"), 0);
        cJSON_ReplaceItemInObject(pool, "health", cJSON_CreateString("DEGRADED"));
        cJSON_ReplaceItemInObject(disk(root, "/dev/sdd"), "smart_passed", cJSON_CreateFalse());
    }
    double ts = cJSON_GetObjectItemCaseSensitive(root, "ts")->valuedouble;
    char *json = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    nas_reset();
    nas_observe(json, ts);
    free(json);
    double now = ts + 2;
    if (strcmp(state, "stale") == 0) {
        nas_observe(NULL, ts + 240);
        now = ts + 240;
    }

    /* gratch's root filesystem, as its telemetry reported it on 2026-11-12. */
    registry_init();
    client_info_t *c = registry_find_or_create(KPIDASH_NAS_HOST);
    snprintf(c->disks[0].label, sizeof(c->disks[0].label), "root");
    c->disks[0].used_gb = 13;
    c->disks[0].total_gb = 913;
    c->disks[0].pct = 1.4f;
    c->disk_count = 1;

    lv_init();
    lv_display_t *disp = lv_display_create(UNIT_W_N(2), UNIT_H);
    static uint8_t buf[1264 * 64 * 4];
    lv_display_set_buffers(disp, buf, NULL, sizeof(buf), LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_flush_cb(disp, flush_cb);
    lv_obj_t *scr = lv_screen_active();
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x11111B), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);

    lv_obj_t *card = nas_card_create(scr);
    lv_obj_set_pos(card, CELL_PAD, CELL_PAD);
    nas_card_update(card, now);

    lv_draw_buf_t *snap = lv_snapshot_take(scr, LV_COLOR_FORMAT_XRGB8888);
    if (!snap) {
        fprintf(stderr, "snapshot failed\n");
        return 1;
    }
    FILE *f = fopen(argv[1], "wb");
    bool ok = f && bmp_write_xrgb8888(f, snap->data, (int)snap->header.w, (int)snap->header.h,
                                      (int)snap->header.stride);
    if (f)
        fclose(f);
    lv_draw_buf_destroy(snap);
    return ok ? 0 : 1;
}
