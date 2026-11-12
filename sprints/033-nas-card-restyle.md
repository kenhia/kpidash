# 033 — NAS card restyle: A · Rail in mauve, disks by use

Proposal korg:4567 covers WI 4566. It ran as a karc leg (kpidash-bc36d4) on
kai under an overseer.

## Goal

Rebuild the NAS card to the "A · Rail" mock-up Ken picked on 2026-11-12, with
his tweaks. The card should sit beside the 10G card as a sibling: the same
skeleton in the NAS's own colour (mauve), no bright green border, a headline
that says the state, eight disk tiles in Ken's order with a use label above
each, and nothing under 20 px. The CPU/RAM chart goes, because gratch has a
host column now.

Design: `planning/nas-card-mockups.html` (function `vA` holds the geometry),
with `planning/net-card.png` and `planning/nas-now.png`, the live cards it was
drawn beside. The mock-up's own data is gratch on 2026-11-12.

## Premise check

| claim | verdict |
|---|---|
| `kdash:nas` v1 carries device, model and kind, but no use or pool membership | **holds.** Read the live record off rpi53. |
| The four NVMe models all differ, the three HDDs share one, and the SSD is unique | **holds.** `Samsung SSD 990 PRO 1TB` (nvme2), `CT4000T710SSD8` (nvme0), `WD_BLACK SN7100 4TB` (nvme1), `Samsung SSD 990 EVO Plus 4TB` (nvme3), `Samsung SSD 860 EVO 500GB` (sda), and `ST8000VN0022-2EL112` ×3 (sdb, sdc, sdd). The mock-up's short names ("T710", "990 PRO") are not what the feed sends, which is why the table matches the full strings. |
| Sprint 032 kept the `graph_host` series and gratch's `dev_telemetry` GET only for this chart | **holds.** The chart was the one reader of each. Both are removed (below). |

## Decisions

- **The disk-to-use table is `src/nas_disk_uses.h`**, a header that holds
  nothing but the table, so it is the one obvious place to edit. Each row maps
  an **exact** model string to a use and a slot. Matching on the model rather
  than the device keeps a disk in its place when nvmeN numbering changes
  across a boot. The three cold HDDs share a row, and the device name breaks
  the tie. A disk the table does not list gets no label and sorts after every
  listed one. Exact match, not substring: a replacement disk is a different
  model and should show up unlabelled rather than quietly take over a slot.
- **Each pool NVMe has its own slot** (1, 2, 3), not one shared "pool" slot.
  So the order is held by the physical disk, not by whatever name it booted
  with.
- **Everything the card decides is in `nasstate.c` and in the gate:** use,
  order, the headline and its sub line, and the reason a disk is in trouble
  (`nas_disk_use`, `nas_disk_order`, `nas_headline`, `nas_disk_reason`). The
  widget only draws.
- **Headline precedence.** A stale feed comes first. Then a pool in trouble
  (`nas DEGRADED`, `nas 3 ERRORS`), then the worst disk (`sdc PENDING 2`,
  `sdd SMART FAILED`, `nvme2 HOT 81°`), then the collector (`COLLECTOR DOWN`,
  `SOURCE STALE`), and otherwise `NAS OK`. The pool comes before the disk
  because a degraded pool is the consequence the reader cares about, which is
  also what the mock-up's crit state shows.
- **Levels are unchanged.** DEGRADED is still WARN in `nas_pool_level` (it
  still serves data), so a pool that is only degraded draws a yellow frame and
  headline. The pool **bar** turns red for any state but ONLINE, as the brief
  says. Changing what DEGRADED means would be a decision about the levels, and
  this sprint is about the look.
- **One pool bar.** gratch has one pool. A second pool would still count
  toward the headline and the frame, but would get no bar of its own.
- **Font sizes:** the temperature is bold 48 (the mock-up's 44 has no
  generated size). The regular 20 lines that can carry "·" are spangroups that
  draw the dot in bold 20, the 10G card's technique. That machinery is a
  ~40-line local copy rather than a shared helper: extracting it would mean
  touching the 10G card, which is out of scope.
- **The frame is its own border-only object**, as on the 10G card's columns,
  so the 3 px attention frame never moves the contents. Surface1 at 2 px when
  healthy, yellow or red at 3 px, muted at 3 px when stale. Never green.
- **Opaque fills only.** The cold tiles use the mock-up's pre-mixed `#2B3046`
  (Base plus 12 % blue), and the bars are opaque track and fill rects.
  Refresh sets only what changed, colours and widths included.
- **Offline renderer** `tests/render_nas_card` (full native tree, like
  `render_host_columns`). It draws the card from the captured live record in
  any of the four states, so the card can be compared with the mock-up without
  a Pi: `build-warnings-full/tests/render_nas_card out.bmp [ok|warn|crit|stale]`.

## What shipped

- `src/widgets/nas_card.c` rewritten to vA. `nas_card.h` comment updated.
- `src/nas_disk_uses.h` (new): the table.
- `src/nasstate.{h,c}`: use, order, headline, disk reason.
- Removed: `graph_host_*` and `dev_telemetry_t` (`registry.{h,c}`),
  `redis_parse_dev_telemetry_json` and the poll's Step 5 GET (`redis.{h,c}`),
  `KPIDASH_KEY_DEV_TELEMETRY`, `tests/test_graph_router.c` and the
  dev_telemetry cases in `test_redis_json.c`. The dashboard now reads no
  `dev_telemetry` key. kdeskdash still does, so the producer stays.
- `tests/fixtures/nas-gratch-2026-11-12.json`: the live record, the fixture for
  both the tests and the renderer.
- Tests (`test_nasstate`): Ken's order and labels from the live record; order
  follows the model when device names swap; an unlisted disk sorts last with no
  label; the four states — ok, disk warning, pool degraded, stale — as headline
  and sub line; pool errors, a hot NVMe, a stale source.
- Docs: CLIENT-PROTOCOL §8 and §8f, ARCHITECTURE (data flow, NAS panel, test
  list, the graph-router paragraph removed), roadmap.
- `sprints/planning/nas-card-mockups.html`, `net-card.png`, `nas-now.png`.

Gate: `just check` green (15 ctest targets, 180 client tests).
`just check-warnings-full` is clean at `-O2 -Werror`. The four state renders
are attached to the proposal (korg img-11d8, img-11d9).

## Repaired in passing

- `sprints/planning/roadmap.md` "Now" still led with sprint 030, although 031
  and 032 had shipped. Rewritten with 033 at the top and 030-032 folded into
  one line, and the "NAS card restyle" entry removed from Next.

## Follow-ups

- Raising the 10G card's 14/16 px labels to the 20 px floor is its own sprint,
  per the proposal notes. It is not filed here.

## Left for the ship turn

The live krpidss capture and the panel CPU check (sprint 032 measured ~6.3 %
of one core) can only be taken once this is deployed to rpi53.
