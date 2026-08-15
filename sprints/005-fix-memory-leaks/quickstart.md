# Quickstart: Memory Leak Soak & Smoke

**Feature**: 005-fix-memory-leaks  
**Date**: 2026-05-16

This guide covers three operations:

1. **Smoke** — a developer-machine check that telemetry works and the regression test passes (minutes).
2. **Soak** — the 24 h on-target acceptance run that decides SC-001 / SC-002 (the next day).
3. **Verdict** — how to compute pass/fail from the captured log.

It assumes the work in [plan.md](plan.md) has been built (`cmake --build build` succeeds, `ctest` is green).

---

## 1. Smoke (developer machine)

Goal: verify telemetry is wired up and the regression guard is sensitive enough to catch a known-bad pattern.

```bash
# Build native
cmake -B build && cmake --build build --parallel

# Run the widget leak regression test
ctest --test-dir build -R test_widget_leak -V
```

Expected: `test_widget_leak` passes — for each covered widget, `lv_mem_monitor.max_used` after 1000 update iterations is within the test's tolerance of the warm value (FR-009, SC-005).

To verify telemetry end-to-end on a developer machine without a real display, start kpidash against a local Redis (use whatever the project's existing dev recipe is) and watch:

```bash
# Log: one memstat: line per minute
journalctl --user -u kpidash -f | grep --line-buffered '^memstat:'

# Redis: current sample
redis-cli GET kpidash:system:mem:current | jq .

# Redis: recent history (newest first)
redis-cli LRANGE kpidash:system:mem:ring 0 4 | jq -s .
```

Pass: first `memstat:` line appears within 60 s of startup (SC-004) and continues at 60 s cadence.

---

## 2. Soak (Raspberry Pi 5, `rpi53` or equivalent)

Goal: produce a 24 h log that proves (or refutes) SC-001, SC-002, SC-003.

1. Cross-compile kpidash for aarch64 and deploy per [`docs/HANDOFF-CROSSCOMPILE.md`](../../docs/HANDOFF-CROSSCOMPILE.md).
2. Ensure the kpidash service is configured to write its log to a known path (e.g. via the existing `logpath` config); the log MUST persist across the 24 h window.
3. Drive representative Redis traffic. Acceptable sources, in preference order:
   - Live production clients pointed at the rpi53 Redis (the most faithful).
   - A captured trace replayed at original timing.
   - The synthetic generator under `scripts/load_test.py` configured to produce client check-ins, activities, and repo status updates at production cadence.
4. Start kpidash and record the start time:
   ```bash
   systemctl --user restart kpidash
   T0=$(date +%s)
   echo "$T0" > /tmp/kpidash-soak-t0
   ```
5. Let it run for 24 h. Do not install tools, do not poke at it (the original incident was caused by an unrelated `apt install`).
6. After 24 h, capture the log:
   ```bash
   journalctl --user -u kpidash --since "@$(cat /tmp/kpidash-soak-t0)" \
     | grep '^memstat:' > soak.log
   ```

---

## 3. Verdict

Apply the rules from [research.md](research.md) R5: the baseline is the sample closest to `T0 + 3600 s`; the final is the last sample.

### awk (no extra deps)

```bash
T0=$(cat /tmp/kpidash-soak-t0)
awk -v t0="$T0" '
  /^memstat:/ {
    for (i=1;i<=NF;i++){split($i,kv,"="); v[kv[1]]=kv[2]}
    if (base_t == "" && v["t"]+0 >= t0+3600) {
      base_t=v["t"]; base_rss=v["rss"]; base_max=v["lvgl_max"]
    }
    last_rss=v["rss"]; last_max=v["lvgl_max"]
  }
  END {
    if (base_t=="") { print "FAIL: soak shorter than 1h warm-up"; exit 2 }
    rss_ratio = last_rss/base_rss
    max_ratio = last_max/base_max
    printf "baseline RSS = %d, final RSS = %d, ratio = %.2f (limit 1.50)\n", base_rss, last_rss, rss_ratio
    printf "baseline LVGL max_used = %d, final = %d, ratio = %.2f (limit 1.20)\n", base_max, last_max, max_ratio
    if (rss_ratio > 1.5)  { print "FAIL SC-001"; exit 1 }
    if (max_ratio > 1.2)  { print "FAIL SC-002"; exit 1 }
    print "PASS SC-001 and SC-002"
  }
' soak.log
```

### Python equivalent

```bash
python3 - <<'PY' soak.log "$(cat /tmp/kpidash-soak-t0)"
import sys
path, t0 = sys.argv[1], int(sys.argv[2])
base = final = None
for line in open(path):
    if not line.startswith("memstat:"): continue
    kv = dict(p.split("=",1) for p in line.split()[1:])
    s = {k: int(v) for k, v in kv.items() if v.isdigit()}
    if base is None and s["t"] >= t0 + 3600: base = s
    final = s
assert base, "FAIL: soak shorter than 1h warm-up"
rss_r = final["rss"] / base["rss"]
max_r = final["lvgl_max"] / base["lvgl_max"]
print(f"RSS ratio {rss_r:.2f} (limit 1.50), LVGL max_used ratio {max_r:.2f} (limit 1.20)")
ok = rss_r <= 1.5 and max_r <= 1.2
print("PASS" if ok else "FAIL")
sys.exit(0 if ok else 1)
PY
```

Additionally verify SC-003 by inspection:

```bash
# No OOM kills attributable to kpidash
journalctl -k --since "@$(cat /tmp/kpidash-soak-t0)" | grep -i 'oom\|killed' || echo "no OOM events"

# Swap not engaged during the run
sar -S 1 1 2>/dev/null || free -h    # ad-hoc check; or compare /proc/meminfo over time
```

A soak is **accepted** iff the verdict prints PASS, no OOM events reference kpidash, and the system did not swap due to kpidash.

---

## Troubleshooting

- **No `memstat:` lines in the log** — verify `LV_USE_MEM_MONITOR` is enabled in `lv_conf.h` and the 60 s timer is registered in `main.c`.
- **`lvgl_max_used` climbs but `lvgl_used` is steady** — leak is in a code path that allocates and frees within a single tick but with growing peak; check chart series / canvas buffer creation.
- **`rss_bytes` climbs but LVGL fields are flat** — leak is outside the LVGL heap. Suspect: `hiredis` reply leaks (missing `freeReplyObject`), `cJSON` (`cJSON_Print*` returns malloc'd strings that must be `free`d), or DRM framebuffer growth.
- **Soak shorter than 1 h** — the verdict script refuses to compute a ratio; rerun the soak.
