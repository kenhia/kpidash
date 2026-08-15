# Findings: 005-fix-memory-leaks

Audit notes and remediation records for the memory-leak investigation.
See [spec.md](spec.md), [research.md](research.md), and the data-model
`leak_source` schema in [data-model.md](data-model.md).

---

## L1. client_card.c — `update_disk_bars` rebuilt LVGL object tree every second

- **Source file:** [src/widgets/client_card.c](../../src/widgets/client_card.c)
- **Suspect call site:** `update_disk_bars(card_handles_t *h, const client_info_t *c)`
- **Pattern:** Delete-and-rebuild on every telemetry update.
- **Discovered by:** Code audit (R6 priority sweep extended beyond the
  initial suspect list).

### Failure mode

`client_card_update_telemetry()` runs from `ui_refresh()` on the 1 s
poll timer. It calls `update_disk_bars()`, which called
`lv_obj_clean(h->disk_cont)` and then created **1 row + 1 bar + 3
labels per disk** (5 LVGL objects per disk per second per card).

Allocation budget per 24 h with 4 client cards × ~4 disks per card:

`4 cards × 4 disks × 5 objects/disk × 86400 s ≈ 6.9 M object create/destroy cycles per day.`

LVGL's static heap copes with the steady-state object count, but the
churn fragments the heap and any per-object metadata that the cleanup
path misses accumulates linearly. This matches the observed RSS climb
from ~67 MB at boot to 7.7 GB after roughly 24 h.

### Fix

Replaced the clean-and-rebuild loop with a persistent disk-row pool
stored on `card_handles_t::disk_rows[]`. Rows are created lazily up to
`MAX_DISPLAY_DISKS` and **reused** across telemetry ticks; extras are
hidden with `LV_OBJ_FLAG_HIDDEN`, never deleted. Each tick updates the
bar value, indicator color, and three label texts in place.

### Verification

- Native build green (`cmake --build build --parallel`).
- Existing unit tests pass (`ctest --test-dir build`: 4/4).
- First on-target soak (2026-05-16 13:01–15:25 UTC, ≈ 143 min) showed
  RSS growth of 68.7 MB → 134.2 MB ≈ **27.5 MB/h** — a ≈12× reduction
  vs. the pre-fix baseline of ~320 MB/h, but evidence the leak was not
  fully closed. See L2.

---

## L2. redis.c — `update_repos` cJSON parse leaked every poll cycle per repo

- **Source file:** [src/redis.c](../../src/redis.c) (around line 470,
  inside the repo-polling loop in `update_repos()` /
  `redis_poll_repos`).
- **Pattern:** `cJSON_Parse` whose return value is never used and
  never freed.
- **Discovered by:** Audit triggered when the L1 fix alone reduced
  the leak rate from ~320 MB/h to ~27 MB/h but did not eliminate it.

### Failure mode

The repo-polling loop did:

```c
cJSON *root = cJSON_Parse(json);
if (!root) continue;

repo_entry_t *re = &g_repos[g_repo_count];
memset(re, 0, sizeof(*re));
strncpy(re->host, ...);
strncpy(re->path, ...);

if (!redis_parse_repo_json(json, re))   /* re-parses internally */
    continue;
g_repo_count++;
/* never frees `root` */
```

`redis_parse_repo_json()` does its own `cJSON_Parse` + `cJSON_Delete`
internally, so the outer `root` was dead code that leaked the entire
JSON tree on every iteration — every poll (1 Hz) × every repo × every
host.

### Fix

Removed the redundant `cJSON_Parse` / `if (!root) continue;` block.
`redis_parse_repo_json()` is the only parse path needed.

### Verification

- Native + cross builds green; ctest 4/4 PASS.
- Second on-target soak started 2026-05-16T22:25:54Z; results in T038
  / T041 evidence record.

---

## Soak evidence (T041)

Soak ran on `rpi53` from **2026-05-16T22:25:54Z** under representative
live traffic (6 clients: kubs0, cleo, rpi53, kwork, kai, kubsdb).

**At T+44.90 h (2026-05-18 ~19:18Z, 2695 memstat samples):**

| Metric                 | Value                                  |
|------------------------|----------------------------------------|
| RSS start              | 65.50 MB                               |
| RSS end                | 66.59 MB                               |
| Δ RSS                  | **+1.1 MB over 44.9 h**                |
| Min RSS / Max RSS      | 65.50 MB / 67.42 MB                    |
| Working-set band       | ~2 MB                                  |
| Slope                  | **24.9 KB/h** (≈ noise floor)          |
| Projected 24 h Δ       | 0.58 MB (**1.01× ratio**)              |
| Projected 30 d Δ       | 17.5 MB                                |
| VSize Δ                | +65.9 MB (expected glibc reservations) |

**Per-4h windows (first RSS in each window):**

```
bin   up_s     rss_MB      Δ from prev bin
0     0        65.500      —
4     14400    66.250      +768 KB
8     28801    65.797      −464 KB
12    43202    66.812     +1040 KB
16    57603    66.562      −256 KB
20    72003    67.375      +832 KB
24    86404    66.922      −464 KB
28    100805   66.672      −256 KB
32    115206   67.266      +608 KB
36    129607   66.391      −896 KB
40    144007   66.500      +112 KB
44    158408   66.234      −272 KB
```

**Verdict: PASS for SC-001, SC-002, SC-003.** RSS oscillates in a
~2 MB band with negative excursions in 5 of 11 4-hour bins, which is
incompatible with a real leak — it is glibc's normal allocate /
`malloc_trim`-style cycle around a stable working set.

Total reduction vs. pre-fix baseline:

| Build          | Slope            | 24 h projected | Ratio vs. start |
|----------------|------------------|----------------|-----------------|
| Pre-fix        | ~320 MB/h        | ~7.7 GB        | ~115×           |
| After L1 only  | ~27.5 MB/h       | ~660 MB        | ~11×            |
| After L1 + L2  | **24.9 KB/h**    | **0.58 MB**    | **~1.01×**      |

That is a **~13 000× reduction** in growth rate. Soak left running to
confirm stability over additional days; sprint close-out pending final
recheck.

### Extended soak (T+163.68 h, 2026-05-23)

Same `kpidash` process, no restart since 2026-05-16T22:25:54Z.

- RSS 65.50 MB → 67.31 MB (Δ +1.81 MB over 163.7 h, slope **11.3 KB/h**)
- Slope *decreased* from 24.9 KB/h (45 h window) to 11.3 KB/h
  (163 h window) as the sample base widened, confirming convergence
  rather than constant-rate growth.
- All deltas remain inside the ~2 MB working-set band observed at
  hour 45.

### SC-003 evidence (T040)

- **No OOM events**: `journalctl --since "2026-05-16 15:25:54" | grep -i oom` → empty.
- **No swap activity**: `vmstat 1 3` shows `si=0 so=0` throughout;
  Pi 5 still has 6.84 GB free of 8 GB total.

### FR-012 evidence (T040b — no performance regression)

- `vmstat` CPU stays at ~15 % user, ~0–1 % system, ~85 % idle — consistent with the
  pre-fix steady state.
- Recent kpidash journal (last 40 non-`memstat:` lines) contains zero
  warnings, errors, or client churn over the soak window — the dashboard
  is happily steady-state. Visual inspection of the rpi53 display
  (confirmed by user during the week-long soak) shows no refresh
  degradation.

---

## No-op audits

The following code paths from research.md R6 were audited by inspection
and found to be already safe (no fix required). The 24 h soak will
confirm.

### repo_status.c (`repo_status_widget_update`)

- Uses a static cache (`s_cache`, `s_cache_count`) keyed on the repo
  data; skips rebuild when `repos_equal()` returns true. Repos change
  on the order of minutes-to-hours, not seconds, so the rebuild path
  fires too rarely to be a leak vector even if it allocates.

### activities.c (`activities_widget_update`)

- Same cache pattern (`s_cache`, `activities_equal`). Rebuild branch
  allocates `elapsed_user_data_t` via `lv_malloc()` for active rows and
  attaches a `LV_EVENT_DELETE` handler (`elapsed_label_delete_cb`)
  that frees the user data and deletes the per-row timer. The cleanup
  path is correct (LVGL fires `LV_EVENT_DELETE` deterministically on
  `lv_obj_delete()`). At ~1 activity transition per minute the
  worst-case churn is ~1.4 k allocations/day, well below any leak
  threshold.

### dev_graph.c (`dev_graph_update`)

- Uses `lv_chart_set_next_value` with `LV_CHART_UPDATE_MODE_SHIFT`,
  which reuses pre-allocated series buffers. Labels are reused via
  `lv_label_set_text`. `dev_graph_destroy` properly `lv_free`s the
  `dev_graph_priv_t` before calling `lv_obj_delete`. Create/destroy
  only happens on dev-graph enable/disable or client switch.

### fortune.c (`fortune_widget_update`)

- Reuses the single label created in `fortune_widget_create` via
  `lv_label_set_text`. No allocation on the update path.

### status_bar.c (`status_bar_show` / `status_bar_hide`)

- Reuses the single label; only updates text, colour, and
  `LV_OBJ_FLAG_HIDDEN`. No allocation on the update path.

### ui.c (`add_card` / `remove_absent_cards`)

- Cards are created on first appearance of a hostname and deleted only
  when a host disappears from the registry. Per-card `card_handles_t`
  is freed via a `LV_EVENT_DELETE` callback (`card_delete_cb`). At a
  typical churn rate (hosts come and go on the order of hours) this is
  not a leak vector.

### Redis error overlay (`g_redis_err` in ui.c)

- Single persistent overlay object created in `ui_init`. Toggled via
  `LV_OBJ_FLAG_HIDDEN`. No allocation on show/hide.

## Observations during T037 soak start

- **LVGL heap counters read 0.** The first memstat samples on `rpi53`
  show `lvgl_used=0 lvgl_max=0 lvgl_total=0 lvgl_frag=0
  lvgl_free_biggest=0`. This is expected: our `lv_conf.h` does not
  enable LVGL's internal allocator (`LV_USE_STDLIB_MALLOC ==
  LV_STDLIB_CLIB`), so LVGL allocates from libc and
  `lv_mem_monitor()` has no internal pool to report on. RSS remains
  the primary signal for SC-001/SC-002; LVGL columns will stay zero
  for the duration of the soak and should not be misread as "no LVGL
  activity."

- **stdout vs. stderr.** Initial deploy emitted no `memstat:` lines
  because `fprintf(stdout, ...)` is block-buffered under
  systemd. Switched memstat output to `fprintf(stderr, ...)` to match
  the rest of the project (registry, status, etc.) and re-deployed.
  Lines now appear in `journalctl -u kpidash` immediately.
