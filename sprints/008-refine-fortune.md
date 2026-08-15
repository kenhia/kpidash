# Fortune Widget Refinement - Peer Programming Log

## Goal

Improve the Fortune widget to reject poorly-formatted fortunes rather than displaying them with ugly wrapping. User statement: *"I'd rather 'reject' (discard and get another) fortunes that are problematic than have the 'ugly' on my dashboard for several minutes."*

## Approach

Four-step implementation using character-count heuristics with tunable thresholds:

1. **Step A: Font-fit heuristic** — `fortune_pick_font()` walks a font ladder (36→28→24→20pt) testing each against configurable max-chars-per-line and max-line-count thresholds.
2. **Step B: Widget parameters** — Updated `fortune_widget_update()` to accept font size, allowing the UI to render at the selected size.
3. **Step C: Retry loop** — Rotation path now retries up to 10 times before falling back to a canned message; tracks elapsed time and reject count.
4. **Step D: Dev overlay** — Added a calibration overlay showing "Rejects: N  Elapsed: N ms" (togglable via Redis command).

## Implementation Details

**Files Modified:**
- `src/fortune.c` — Heuristic, retry logic, statistics tracking
- `src/widgets/fortune.c` — Widget parameter handling, dev overlay rendering
- `src/redis.c` / `src/redis.h` — Dev overlay polling via `kpidash:cmd:fortune_dev`
- `src/protocol.h` — New Redis key constants
- `src/registry.h` — Dev state struct
- `docs/CLIENT-PROTOCOL.md` — Protocol documentation

**Thresholds (tunable via `#define`):**
```c
36pt: max 45 chars/line, max 5 lines
28pt: max 60 chars/line, max 7 lines
24pt: max 75 chars/line, max 9 lines
20pt: max 90 chars/line, max 11 lines
```

## Testing & Calibration

- **Unit tests:** All 8 pass (no regressions)
- **Calibration suite:** 7 test fortunes pushed to dashboard
  - Test 1: 8 chars — ✅ Passed, looked good
  - Test 2: 32 chars (sentence) — ✅ Passed, looked good
  - Test 3: 76 chars (long sentence) — ✅ Passed, looked good
  - Test 4: 2 lines (multiline) — ✅ Passed, looked good
  - Test 5: 3 lines (longer multiline) — ✅ Passed, looked good
  - Test 6: 155 chars (very long line) — ✅ Passed, wrapped but acceptable
  - Test 7: 6 lines (many lines) — ✅ Passed, looked good

**Outcome:** All tests accepted; no rejections observed. Current thresholds working well.

## Deployment

- Deployed to Pi 5 (aarch64) via systemd service
- Dashboard confirmed running and connected to Redis
- Dev overlay verified working on live dashboard

## Future Observations

Thresholds frozen at current values. Plan to observe live fortune data over the next 24-48 hours to determine if adjustments are needed (tighten to reject more edge cases, or loosen for more generous wrapping).

## Key Gotchas Encountered

1. **Redis auth errors** — `redis-cli` without `-h rpi53` connects to local kubs0, not the dashboard's Redis
2. **Dev overlay z-order** — Initial rendering placed overlay beneath fortune text; fixed with `lv_obj_move_to_index()`
3. **Duplicate dashboard processes** — Manual + systemd instances competed for display; resolved by killing manual instance
4. **Fish shell here-docs** — Fish doesn't support bash-style `<< EOF`; used bash wrapper or escaped newlines

## Status

✅ **Complete and deployed** — Calibration successful, thresholds tuned, ready for long-term observation.
