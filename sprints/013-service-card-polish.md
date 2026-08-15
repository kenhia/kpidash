# Sprint 013 — service-card polish

**korg proposal**: korg:379 — *kpidash service-card polish — self-card icon + list/prune tool*
**Branch**: `013-service-card-polish`
**Status**: active

Small follow-on to sprint 012. Two dashboard-side items on the service-card path.

---

## Order of work

### 1. #375 — nerdfont icon `f0a07` on the self-card  _(task / S)_
Add the Material-Design nerdfont glyph **`f0a07`** ("nf-md-monitor_dashboard", U+F0A07)
and show it on the `rpidash` self-service card (#369).

- The icons font [fonts/lv_font_icons_56.c](../../fonts/lv_font_icons_56.c) currently
  only covers `0xF300–0xF381` + `0xF1D2–0xF1D3` — **U+F0A07 is outside that**, so
  [fonts/generate.sh](../../fonts/generate.sh) needs a range covering `0xF0A07`
  (and the montserrat text fonts don't need it). Regenerate.
- Register the glyph in [src/icons.c](../../src/icons.c) / [src/icons.h](../../src/icons.h)
  at a new `icon_index`.
- Have [src/redis.c](../../src/redis.c) `redis_publish_self_service()` include
  `"icon": <index>` so the self-card renders it (service cards already draw
  `icon_index` via `icons_lookup`).

### 2. #374 — service-card list + selective prune tool  _(feature / S)_
List all service (and apt-temps) cards with **age** (now − `ts`), and let the
operator **selectively prune** stale ones. **Manual, not auto-evict** — a stopped
service may stay as a reminder.

- **List** (client-side): SCAN `kpidash:services:*:*` and `kpidash:apttemps:*`,
  parse `ts`, show `(name, host, state, age)`. No dashboard cooperation needed.
- **Prune**: `DEL` the key — but the in-memory card lingers until restart. To make
  prune restart-free, add a dashboard **drop-from-registry command**
  (e.g. `kpidash:cmd:services:evict` consumed each poll → remove `(name,host)`
  from `g_services[]`, destroy the card on the LVGL thread). Mirror #250's
  `graph_host_remove` pattern; cover `g_apttemps[]` too.
- Likely a `kpidash-client` subcommand or a `scripts/` helper.

---

## Notes
- Both touch `src/registry.c` / `src/redis.c` (service + apt-temps registries).
- Prior art: sprint 012 #250 (`graph_host_remove` + eviction) is the template for
  the prune/drop mechanism.
- Close-out: [.korg-sprint-proposal](../../.korg-sprint-proposal) holds `korg:379`.
