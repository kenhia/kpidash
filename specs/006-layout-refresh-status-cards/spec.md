# Feature Specification: Layout Refresh, Service Status Cards, and Graph Polish

**Feature Branch**: `006-layout-refresh-status-cards`
**Created**: 2026-05-23
**Status**: Draft
**Input**: User description: "Sprint 006 — reshape the kpidash visible layout, add a Service Status Cards widget class in the footer, relocate Fortune into the rows 2-3 widget pool, and polish the Graph widget (always-on, stale-data overlay, per-host instances, thicker GPU traces). Includes a new `kpidash-mockup` build target for iterating on footer card visuals."

## Clarifications

### Session 2026-05-23

- Q: How should the dashboard handle a Service Status payload that is missing the `state` field or carries an unrecognized `state` value? → A: Ignore the malformed payload (do not update the card); the most recent valid state continues to drive the card's border color and ages out of the 60-second window normally. The dashboard MAY log the malformed payload for debugging but MUST NOT surface it visually.
- Q: What should happen to a Graph widget whose host has stopped reporting? → A: The widget remains in rows 2-3 indefinitely with the "NO NEW DATA" overlay; there is no eviction timeout. The widget is only removed when the dashboard restarts (and the host is not re-discovered) or via future explicit operator action (out of scope this sprint).

## User Scenarios & Testing *(mandatory)*

### User Story 1 - Operator sees the new layout at a glance (Priority: P1)

When the operator looks at the 4K dashboard, the screen is immediately understandable: Row 1 shows per-client status, the middle of the screen shows the operationally important widgets (graphs first, then activities, repo status, and fortune), and the bottom strip shows a row of small service status cards whose border colors instantly communicate which back-end services are healthy.

**Why this priority**: This is the primary deliverable of the sprint — the new visible composition of the dashboard. Without it, none of the other items have a place to live.

**Independent Test**: Deploy the build to the Pi, point it at a Redis instance that has at least one client reporting, one graph host reporting, and one service reporting. Confirm Row 1 contains only Client Cards, rows 2-3 contain widgets placed in the documented priority order, and the footer contains Service Status Cards.

**Acceptance Scenarios**:

1. **Given** the dashboard is running with one client reporting, one graph host reporting, and one service reporting, **When** the operator looks at the screen, **Then** Row 1 shows only Client Cards, rows 2-3 contain the Graph for that host plus any other widgets in priority order, and the footer shows a Service Status Card for the reporting service.
2. **Given** more widgets are requested for rows 2-3 than the 12 available cells can hold, **When** the dashboard composes the layout, **Then** the lowest-priority widgets are dropped and the remaining widgets are placed in priority order without resizing.
3. **Given** only the Graph and Fortune are requested for rows 2-3, **When** the dashboard composes the layout, **Then** Graph is placed first and Fortune appears as a 2×1 widget elsewhere in rows 2-3.

---

### User Story 2 - Operator sees a Service Status Card go red when a service stops reporting (Priority: P1)

A back-end service (for example, a long-running ingest job) stops sending its periodic status update. Within roughly a minute, the corresponding card in the footer changes its border from green to red, alerting the operator that the service is no longer reporting and was not intentionally shut down.

**Why this priority**: Service Status Cards are the most novel widget in this sprint and the freshness/color logic is the part most likely to surprise the operator if wrong. Verifying the green→red transition validates the data model, the freshness check, and the rendering pipeline end-to-end.

**Independent Test**: From a developer machine, run `kpidash-client service-status` once to publish a healthy report for a test service, observe the green card appear, stop publishing, and observe the card border transition to red within 60–62 seconds.

**Acceptance Scenarios**:

1. **Given** no card exists for service `my-service`, **When** `kpidash-client service-status` publishes an `ok` report for `my-service`, **Then** a green-bordered Service Status Card for `my-service` appears in the footer within 2 seconds.
2. **Given** a green card for `my-service` is visible and the service has stopped publishing, **When** 60–62 seconds have elapsed since the last published timestamp, **Then** the card border is red.
3. **Given** a card for `my-service` is visible, **When** the service publishes `state: down`, **Then** the card border is gray and remains gray indefinitely (no red transition) as long as the `down` report is the most recent.
4. **Given** a card for `my-service` is visible, **When** the service publishes `state: unhealthy`, **Then** the card border is yellow within 2 seconds.
5. **Given** a card for `my-service` is visible, **When** the service publishes `state: maintenance`, **Then** the card border is blue within 2 seconds.

---

### User Story 3 - Operator immediately notices a stalled graph feed (Priority: P1)

The Graph widget is always visible (there is no on/off toggle in the UI). If data for a host stops arriving, the displayed traces freeze and a centered "NO NEW DATA" label appears over that host's graph so the operator does not mistake stale traces for current readings.

**Why this priority**: A frozen graph without indication is actively misleading. This requirement is functionally a safety/correctness fix as well as a polish item.

**Independent Test**: Start `kpidash-client` graph reporting from one host, confirm the graph renders, stop the client, and confirm the "NO NEW DATA" overlay appears within ≤ 35 seconds.

**Acceptance Scenarios**:

1. **Given** a host is publishing graph samples and the host's Graph widget shows live traces, **When** publishing stops, **Then** within ≤ 35 seconds the displayed traces freeze and a centered "NO NEW DATA" label is overlaid on that host's graph.
2. **Given** the "NO NEW DATA" overlay is visible for a host, **When** the host resumes publishing samples, **Then** the overlay disappears and the traces resume updating.
3. **Given** the dashboard is running, **When** the operator inspects the UI, **Then** no Graph on/off control is present.

---

### User Story 4 - Operator sees one Graph per reporting host (Priority: P2)

When both `kdash` and `kai` are publishing graph samples, the dashboard renders two Graph widgets — one per host — placed in rows 2-3 in alphabetical order by host name (`kai` before `kdash`). GPU usage and GPU VRAM traces are visibly thicker than the other traces so the operator's most relevant signals stand out.

**Why this priority**: Multi-host graphs depend on a Redis schema change and on multiple instances being placed by the layout engine. The thicker GPU traces are a small visual change but materially improve readability of the most-watched signals.

**Independent Test**: Configure `kpidash-client` on both `kai` and `kdash` to publish graph samples, observe two Graph widgets, confirm `kai` is placed before `kdash`, and confirm GPU usage and GPU VRAM trace lines are visibly thicker than the other traces.

**Acceptance Scenarios**:

1. **Given** both `kai` and `kdash` are publishing graph samples, **When** the dashboard renders, **Then** two Graph widgets are visible and `kai`'s widget is placed before `kdash`'s in rows 2-3.
2. **Given** only `kdash` is publishing, **When** the dashboard renders, **Then** exactly one Graph widget is visible.
3. **Given** a Graph widget is visible, **When** the operator looks at the traces, **Then** the GPU usage and GPU VRAM traces are visibly thicker than the other traces.

---

### User Story 5 - Fortune is readable from across the room (Priority: P3)

Fortune is no longer hidden in a thin footer strip. It now occupies a 2×1 cell within rows 2-3 and uses a font large enough to be read at the dashboard's normal viewing distance.

**Why this priority**: This is a quality-of-life improvement; the dashboard is functional without it but the move out of the footer is necessary for the footer to be reused by Service Status Cards.

**Independent Test**: With Fortune in the requested-widgets list, observe that Fortune renders as a 2×1 cell in rows 2-3 with a noticeably larger font than the previous footer version.

**Acceptance Scenarios**:

1. **Given** Fortune is in the requested-widgets list and there is space in rows 2-3, **When** the dashboard renders, **Then** Fortune appears as a 2×1 widget (2 columns × 1 row) with the larger fixed font.
2. **Given** rows 2-3 are full and Fortune is the lowest-priority requested widget, **When** the dashboard composes the layout, **Then** Fortune is dropped and no fortune is shown.

---

### User Story 6 - Developer iterates on footer card visuals without deploying (Priority: P2)

A developer working on Service Status Cards wants to tune card width, border thickness, icon size, and font choices quickly on the local X11 build without deploying to the Pi.

**Why this priority**: This is a developer-velocity item that directly accelerates the sprint's polish work. It is not user-visible on the deployed dashboard but is a deliverable of the sprint.

**Independent Test**: On the developer's native machine, build the `kpidash-mockup` target and run the resulting binary. Confirm it opens an LVGL X11 window that renders only the footer area populated with stubbed Service Status Cards.

**Acceptance Scenarios**:

1. **Given** the workspace is configured for the native (X11) build, **When** the developer builds the mockup target, **Then** the build succeeds and produces a runnable binary.
2. **Given** the mockup binary is launched, **When** it opens its window, **Then** only the footer region is rendered and it is populated with stubbed Service Status Cards across the available width.
3. **Given** a cross-compile build for the Pi target is invoked, **When** the build completes, **Then** the mockup target is not built or deployed.

---

### Edge Cases

- A service publishes a payload missing the `state` field or with an unrecognized `state` value: the malformed payload is ignored; the card continues to display the most recent valid state and ages out of the 60-second freshness window normally (resolved 2026-05-23).
- A service publishes a payload with a future timestamp: the freshness check still treats the report as "within the last 60 seconds" — clock skew between publishers and the dashboard is out of scope.
- A service stops publishing while in `unhealthy`, `maintenance`, or `down`: once the most recent report ages out of the 60-second window, the card border transitions to red unless the most recent report was `down` (which stays gray indefinitely per spec).
- A host appears in the graph stream, publishes briefly, then disappears: the host's Graph widget remains in rows 2-3 indefinitely with the "NO NEW DATA" overlay; there is no eviction timeout. The widget is only removed at the next dashboard restart if the host is not re-discovered (resolved 2026-05-23).
- The number of services reporting concurrently exceeds the footer's chosen card capacity at the chosen card width: explicit overflow behavior (truncate, scroll, shrink cards) is not specified for this sprint.
- A service publishes a card icon index that is not present in the dashboard's icon registry: the card renders with no icon and the rest of the card is unaffected.
- The Graph schema change (adding `host` to each sample) lands before clients are upgraded: legacy samples without a `host` field are assigned to a synthetic host label so they remain visible until clients are upgraded.

## Requirements *(mandatory)*

### Functional Requirements

#### Layout

- **FR-001**: The dashboard MUST place Client Cards exclusively in Row 1 of the 6×3 grid; no other widget class may occupy Row 1.
- **FR-002**: The dashboard MUST place all non-Client-Card widgets into a single rows 2-3 pool of 12 grid cells.
- **FR-003**: The dashboard MUST place widgets into the rows 2-3 pool in the following fixed priority order (highest to lowest): Graph, Activities, Repo Status, Fortune.
- **FR-004**: When more widgets are requested than fit in rows 2-3, the dashboard MUST drop the lowest-priority widgets that do not fit and place the remaining widgets without resizing them.
- **FR-005**: The dashboard MUST render the footer region (276 px tall) as the container for Service Status Cards; the previous Fortune-in-footer rendering MUST be removed.
- **FR-006**: The existing status/gutter area MUST remain unchanged by this sprint.

#### Fortune

- **FR-007**: Fortune MUST render as a 2×1 widget (2 columns × 1 row) within the rows 2-3 pool.
- **FR-008**: Fortune MUST use a fixed font size larger than the previous footer-Fortune font, chosen for readability of a 2×1 cell at the dashboard's normal viewing distance.
- **FR-009 (stretch)**: Fortune MAY implement simple dynamic font sizing in which shorter fortunes use a larger font up to a maximum, and fortunes likely to be clipped at the chosen font are discarded and replaced by a fresh fortune fetch. Occasional clipping is acceptable; this requirement may be deferred without blocking the sprint.

#### Graph

- **FR-010**: The dashboard MUST NOT expose any user-facing control to toggle the Graph widget on or off; Graph is always rendered when at least one host is reporting graph samples.
- **FR-011**: The Graph data payload schema in Redis MUST include a `host` field on each sample identifying the reporting host.
- **FR-012**: The dashboard MUST render one Graph widget per host currently reporting graph samples.
- **FR-013**: Multiple Graph widgets MUST be placed within the rows 2-3 pool in ascending alphabetical order by host name.
- **FR-014**: When a host's Graph widget has received no new sample for at least 30 seconds, the dashboard MUST freeze the displayed traces and overlay a centered "NO NEW DATA" label on that host's Graph widget.
- **FR-015**: When new samples resume arriving for a host, the dashboard MUST remove the "NO NEW DATA" overlay and resume updating the traces.
- **FR-015a**: A Graph widget whose host has stopped reporting MUST remain in rows 2-3 indefinitely with the "NO NEW DATA" overlay; the dashboard MUST NOT evict such widgets during a running session. The widget is only removed at the next dashboard restart if the host is not re-discovered.
- **FR-016**: The Graph widget MUST render the GPU usage and GPU VRAM traces with a line thickness 2–3× the thickness of the other traces.

#### Service Status Cards

- **FR-017**: The dashboard MUST render Service Status Cards in the footer region, sized to fit approximately 18 cards across the usable footer width (final sizing determined during mockup work).
- **FR-018**: Each Service Status Card MUST display the service label, an optional host name, an optional icon, and a short status text.
- **FR-019**: Each Service Status Card MUST display a thick colored border indicating current service state, using the following mapping:
  - Green — last report within 60 s and `state: ok`
  - Yellow — last report within 60 s and `state: unhealthy`
  - Blue — last report within 60 s and `state: maintenance`
  - Gray — most recent report is `state: down` (no freshness window applied)
  - Red — no report received within 60 s AND most recent report is not `state: down`
- **FR-020**: The dashboard MUST perform the 60-second freshness check itself by comparing the payload's timestamp to current dashboard wall-clock time. Redis-key TTLs MUST NOT be relied upon.
- **FR-021**: The dashboard MUST read service state from Redis keys under a documented namespace following the existing `kpidash:system:*` convention (e.g. `kpidash:services:<name>`).
- **FR-022**: Each service payload MUST be JSON containing at minimum: a timestamp, a `state` field (`ok` | `unhealthy` | `maintenance` | `down`), and a short status text. The payload MAY additionally include a `host` field and an integer `icon` field.
- **FR-022a**: A service payload that is missing the `state` field or carries an unrecognized `state` value MUST be ignored by the dashboard; the most recent valid state MUST continue to drive the card and ages out of the 60-second freshness window normally. The dashboard MAY log such payloads for debugging but MUST NOT surface them visually.

#### Icon registry

- **FR-023**: The dashboard MUST own a registry mapping integer indices to nerd-font glyph codepoints. Clients MUST send only the integer index; clients MUST NOT need to know the glyph codepoint.
- **FR-024**: The icon registry MUST be backed by a curated subset of nerd-font glyphs compiled into the kpidash binary via the existing `fonts/generate.sh` / `lv_font_conv` pipeline.
- **FR-025**: The icon font size MUST be chosen to fit the card icon area (target ~48–64 px; finalized during mockup work).
- **FR-026**: An icon index sent by a client that does not appear in the registry MUST be ignored; the card MUST render without an icon and the rest of the card MUST be unaffected.

#### Client

- **FR-027**: The existing `clients/kpidash-client` (Python) MUST gain a new `service-status` subcommand. A separate `kpidash-service` binary MUST NOT be created.
- **FR-028**: The `service-status` subcommand MUST reuse the existing client configuration mechanism for Redis connection details.
- **FR-029**: The `service-status` subcommand MUST accept arguments for service name, optional host, state, optional icon index, and short status text, and MUST write the resulting JSON payload to the documented Redis key for that service name.

#### Mockup tooling

- **FR-030**: The build system MUST provide a separate target (suggested name `kpidash-mockup`) that produces a binary which boots LVGL on the native (X11) build and renders only the footer region populated with stubbed Service Status Cards.
- **FR-031**: The `kpidash-mockup` target MUST NOT be built or deployed by the Pi cross-compile build path.

#### Quickstart documentation

- **FR-032**: The sprint MUST update the quickstart documentation to describe the configuration needed to enable graph reporting from `kai` (where `kpidash-client` is already deployed).

### Key Entities

- **Layout pool (rows 2-3)**: A fixed 12-cell region (6 columns × 2 rows) into which non-Client-Card widgets are placed in priority order. Has no dynamic resize behavior this sprint.
- **Graph host series**: A per-host stream of graph samples in Redis identified by the new `host` field on each sample. Drives one Graph widget per distinct host.
- **Service**: A back-end process or job that periodically publishes a JSON status payload to a Redis key under the documented services namespace. Identified by service name and optionally annotated with host.
- **Service status payload**: JSON document containing a timestamp, a `state` value (`ok` | `unhealthy` | `maintenance` | `down`), a short status text, and optionally a `host` and an integer `icon` index.
- **Icon registry**: Dashboard-owned mapping from integer index to nerd-font glyph codepoint. Loaded at startup (from a resource file) or compiled in; clients only know integer indices.
- **Service Status Card**: Footer-rendered widget instance per known service. Displays label, optional host, optional icon, status text, and a thick colored border whose color is computed from the most recent payload plus a dashboard-side freshness comparison.
- **Mockup target**: A separate native-only build artifact used to iterate on Service Status Card visuals without deploying to the Pi.

## Success Criteria *(mandatory)*

### Measurable Outcomes

- **SC-001**: After deployment, Row 1 contains only Client Cards, rows 2-3 contain widgets placed in the documented priority order, and the footer contains Service Status Cards — verifiable by visual inspection of the running dashboard.
- **SC-002**: No user-facing Graph on/off control is present in the running dashboard.
- **SC-003**: When graph samples for a host stop arriving, the "NO NEW DATA" overlay becomes visible on that host's Graph widget within ≤ 35 seconds of the last sample (30 s threshold plus one poll cycle of slack).
- **SC-004**: When both `kdash` and `kai` are reporting graph samples, exactly two Graph widgets are visible; when only one of the two is reporting, exactly one Graph widget is visible.
- **SC-005**: GPU usage and GPU VRAM trace lines render visibly thicker than the other Graph traces (operator can identify them at a glance from normal viewing distance).
- **SC-006**: Submitting a `kpidash-client service-status` update for a new service causes a Service Status Card for that service to appear in the footer within ≤ 2 seconds with the correct border color for the reported state.
- **SC-007**: A service whose last report was `ok`, `unhealthy`, or `maintenance` and which then stops reporting has its card border transition to red within 60–62 seconds of its last report's timestamp.
- **SC-008**: A service whose most recent report is `state: down` retains a gray card border indefinitely while that remains the most recent report.
- **SC-009**: Fortune renders as a 2×1 widget within rows 2-3 with a font visibly larger than the previous footer-Fortune font.
- **SC-010**: The `kpidash-mockup` target builds and runs on the native developer machine and renders only the footer populated with stubbed Service Status Cards.
- **SC-011**: The "how the new stuff looks together" coherence check passes: the operator agrees that the new footer, the relocated Fortune, and the polished Graph widgets read as a coherent dashboard rather than three disconnected changes. (Subjective sign-off by the user.)

## Assumptions

- The 4K display geometry (3840×2160, 6×3 grid, 276 px footer) defined in `src/layout.h` is unchanged.
- The existing client-card placement logic for Row 1 is reusable as-is; only the rows 2-3 pool and the footer change in this sprint.
- The Redis instance and connection configuration used by the dashboard and existing clients is reused unchanged; no new Redis deployment is required.
- The existing `fonts/generate.sh` / `lv_font_conv` toolchain can produce a nerd-font subset usable by LVGL without changes to the font pipeline itself. A binary size increase of a few hundred KB from the nerd-font subset is acceptable on the Pi 5 target (RAM headroom verified from sprint 005).
- The number of services reporting concurrently will not exceed the footer's chosen card capacity for the foreseeable future; explicit overflow handling for the footer is not required in this sprint.
- The dashboard's wall-clock time is reasonably accurate; cross-host clock skew is not corrected for the 60 s freshness check.
- Service publishers will write their payload to the documented Redis key with each update (no incremental/append semantics are required from Redis).
- The "NO NEW DATA" detection latency budget (≤ 35 s) allows one full poll cycle of slack on top of the 30 s threshold.
- The stretch goal for dynamic Fortune font sizing (FR-009) may be deferred without blocking sprint completion.
- The `host` field added to graph samples is a free-form string identifier matching what clients already use elsewhere (e.g. `kai`, `kdash`); no host registry is required.
- The mockup target is for developer use only and does not need to be hardened for end-user distribution.
