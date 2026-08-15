# Phase 0 Research — Sprint 006

All technical questions raised by the plan are resolved below. No item is deferred.

---

## R1. Nerd-font glyph subset and `lv_font_conv` invocation for icon glyphs

**Decision**: Add a new icon-only font generated from the already-vendored `fonts/ttf/SymbolsNerdFont-Regular.ttf` at a single size in the 48–64 px range (start at **56 px**, retune during mockup). Generate as `fonts/lv_font_icons_56.c` via `lv_font_conv`, BPP=4, no prefilter, no compression — matching the existing montserrat invocations in `fonts/generate.sh`. The curated codepoint range is the initial set used today by the existing montserrat fonts (`0xF300-0xF381` Nerd-Font "octicons" block plus `0xF1D2-0xF1D3` for power/sync), with room to extend during mockup. The icon registry (see contracts/icon-registry.md) maps a stable integer index to each picked codepoint so clients only ship integers.

**Rationale**:
- Reuses the existing, working `fonts/generate.sh` toolchain (no new dependency, no pipeline rewrite).
- A dedicated icon font at one large size keeps generated-file size predictable (a few hundred KB at 56 px @ BPP=4 for ~32 glyphs) and avoids embedding the icon range into every Montserrat size, which today already includes `0xF300-0xF381` and bloats the small sizes unnecessarily.
- Sprint 005 verified the Pi 5 has ample RAM headroom; a single ~few-hundred-KB font is within budget.
- BPP=4 matches existing fonts so the LVGL renderer path is unchanged.

**Alternatives considered**:
- Continue piggybacking icons onto each Montserrat size (status quo). Rejected: forces an icon-size choice per Montserrat size, none of which is large enough for the card icon area, and bloats unrelated small fonts.
- Use raster PNGs from `resources/` for icons. Rejected: clients would need to know image filenames; integer-index→glyph keeps payload small and centralises the visual catalogue.
- Generate multiple icon font sizes for future flexibility. Rejected (YAGNI / Principle V): one size suffices for the only consumer this sprint (Service Status Card).

---

## R2. LVGL approach for the "NO NEW DATA" overlay on a chart widget

**Decision**: Each Graph widget is a parent `lv_obj_t` container that holds (a) the existing `lv_chart` and (b) a sibling `lv_label` positioned at `LV_ALIGN_CENTER`, created up-front with `lv_obj_add_flag(label, LV_OBJ_FLAG_HIDDEN)`. The freshness check in `dev_graph_update()` toggles the hidden flag based on `now - last_sample_ts >= 30.0`. When showing, the label uses the existing Montserrat Bold 36 (or 48) font, a semi-opaque dark background pad, and white text — implemented purely with LVGL style properties (no custom draw callback).

**Rationale**:
- LVGL 9 supports `LV_OBJ_FLAG_HIDDEN` toggling with O(1) cost and a redraw triggered automatically; no per-frame work.
- Putting the label as a sibling above the chart (z-order: last-added is on top, or `lv_obj_move_foreground()` after creation) means the chart can keep drawing frozen samples without any per-pixel intervention.
- Reusing an existing Montserrat size avoids generating a new font for one label.
- The 30 s threshold is checked in the 1 s UI refresh callback, so worst-case detection is ~30 s + one poll cycle (well under the SC-003 budget of 35 s).

**Alternatives considered**:
- Custom `lv_chart` draw event callback to render the label inside the chart. Rejected: more code for no visual gain; harder to test in isolation.
- Replace the chart with a placeholder widget on stale. Rejected: destroys cached chart state and causes a visible flicker on resume.
- Tint the chart traces grey when stale. Rejected: too subtle for a 4K dashboard at room distance — the spec asks for an unambiguous overlay.

---

## R3. Native X11 mockup target — separate `add_executable` vs build flag

**Decision**: Add a **separate `add_executable(kpidash-mockup ...)`** in the root `CMakeLists.txt`, guarded by `if(NOT CMAKE_CROSSCOMPILING AND NOT TESTS_ONLY)`. The mockup target compiles `src/mockup_main.c` plus `src/widgets/service_card.c`, `src/icons.c`, the icon font, and the LVGL library — but does NOT compile `src/main.c`, `src/redis.c`, `src/status.c`, `src/ui.c`, or any DRM/KMS-only code. The mockup binary opens an LVGL X11 window sized to the footer region only and populates it with stubbed Service Status Cards (no Redis, no clients, fixed test data).

**Rationale**:
- A second `add_executable` keeps the mockup's source list explicit and small, so adding/removing a card variant is a one-file edit.
- Guarding on `CMAKE_CROSSCOMPILING` directly satisfies FR-031 (Pi cross-compile MUST NOT build it) without an extra user-facing option.
- `TESTS_ONLY=ON` (existing fast-test build) already skips LVGL — exclude the mockup there too so CI doesn't pull X11.
- Separate executable means no `#ifdef MOCKUP` clutter in shared code; the mockup gets its own `main()` that calls a small subset of widget code directly.

**Alternatives considered**:
- A single binary with `--mockup` CLI flag. Rejected: forces the production binary to link mock data and complicates the X11/DRM backend selection.
- A CMake option `BUILD_MOCKUP=ON` defaulting OFF. Rejected: easy to forget; auto-detection from `CMAKE_CROSSCOMPILING` is the right default and is more aligned with Principle V.
- Put the mockup under a separate subdirectory with its own CMakeLists. Rejected: overkill for a single source file that shares headers with the production tree.

---

## R4. Service Status Card Redis key naming convention

**Decision**: Use `kpidash:services:<name>` (note the **plural** `services`, mirroring `kpidash:activities`). The value is a single JSON string (no hash, no zset). No Redis TTL is set — freshness is computed dashboard-side by comparing the payload's `ts` field to wall-clock time (FR-020). Service discovery is done by `SCAN MATCH kpidash:services:*` on each dashboard poll cycle, mirroring how the activities and clients keys are enumerated today.

**Rationale**:
- The existing namespace divides keys into `kpidash:system:*` (dashboard-owned metadata), `kpidash:client:<host>:*` (per-host client telemetry), `kpidash:clients` / `kpidash:activities` (top-level collections), and `kpidash:cmd:*` (operator commands). Services are a top-level collection of named entities — the closest existing analogue is `kpidash:activities` (plural), so `kpidash:services:<name>` fits the established shape.
- A single JSON string per service is the simplest representation that carries timestamp + state + text + optional host + optional icon in one atomic write (FR-022). Hash fields would force multiple roundtrips for an atomic update.
- No TTL because FR-020 forbids relying on TTL-based eviction; the dashboard's 60 s freshness check is wall-clock-based. The card may keep the key around indefinitely while the most recent payload is `state: down` (gray).
- `SCAN` instead of a tracking set keeps publishers' write path to a single `SET` — no membership bookkeeping to drift.

**Alternatives considered**:
- `kpidash:service:<name>` (singular). Rejected for inconsistency with `kpidash:activities` and `kpidash:clients`.
- `kpidash:system:service:<name>` under the system namespace. Rejected: `kpidash:system:*` is dashboard-self-published data; services are externally published.
- HASH with separate fields. Rejected: not atomic on partial update and forces the dashboard to do multiple `HGET`s per service per poll.
- Use Redis TTL = 65 s and let key expiry drive removal. Rejected explicitly by FR-020 (and by FR-015a's "no eviction" parallel for graph hosts — same philosophy).
- Maintain a `SADD kpidash:services <name>` companion set. Rejected: extra write per publish for no benefit over `SCAN`, and `SCAN` is what the rest of the dashboard already uses.

---

## Summary

| ID | Topic | Decision (one line) |
|----|-------|---------------------|
| R1 | Icon font | New `lv_font_icons_56` from `SymbolsNerdFont-Regular.ttf` via existing `fonts/generate.sh`, BPP=4. |
| R2 | NO NEW DATA overlay | Sibling `lv_label` on top of the chart, toggled by `LV_OBJ_FLAG_HIDDEN` when `now - last_sample_ts ≥ 30 s`. |
| R3 | Mockup target | Separate `add_executable(kpidash-mockup ...)` guarded by `NOT CMAKE_CROSSCOMPILING AND NOT TESTS_ONLY`. |
| R4 | Services key | `kpidash:services:<name>` (plural), single JSON string, no TTL, discovered via `SCAN MATCH`. |

No NEEDS CLARIFICATION items remain.
