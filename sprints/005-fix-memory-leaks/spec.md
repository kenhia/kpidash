# Feature Specification: Identify and Fix Memory Leaks

**Feature Branch**: `005-fix-memory-leaks`  
**Created**: 2026-05-16  
**Status**: Draft  
**Input**: User description: "Identify and fix memory leaks. Started looking at a memory issue/leak on rpi53 (host for kpidash). When I installed a tool to help debug, `kpidash` restarted and the bulk of the memory was freed. Please review the conversation response from M365 CP `.scratch/mem-leak.md` and create spec to investigate and fix possible memory leaks."

## User Scenarios & Testing *(mandatory)*

### User Story 1 - Dashboard remains stable for weeks of continuous operation (Priority: P1)

The kpidash display is intended to run unattended on a Raspberry Pi 5 (rpi53) as a wall-mounted KPI dashboard. An operator should be able to power it on and forget about it: the dashboard keeps refreshing data from Redis indefinitely without the host running out of memory, swapping, or being restarted by systemd / the OOM killer.

**Why this priority**: This is the core promise of the product. A field observation showed the host climbed to 7.7 GB resident memory (vs. a baseline of ~67 MB for the kpidash process) before an unrelated `apt` install incidentally restarted the service and freed the memory. Left unaddressed, the dashboard will eventually crash, freeze, or be killed under memory pressure — defeating its purpose as an always-on display.

**Independent Test**: Run kpidash on the target hardware (or equivalent) for an extended soak period with live Redis traffic feeding the same widgets used in production. Sample process memory (RSS/PSS/USS) and LVGL heap usage at regular intervals. Pass condition: memory usage reaches a steady state within the first hour and stays within a bounded envelope for the remainder of the soak.

**Acceptance Scenarios**:

1. **Given** kpidash has just started on the target host, **When** it runs continuously for 24 hours under representative Redis traffic, **Then** process RSS at the end of the run is within the bounded envelope of the steady-state value observed after the first hour (see SC-001).
2. **Given** kpidash is running normally, **When** an operator inspects host memory at any point during the run, **Then** kpidash is not the dominant consumer of system RAM and the host is not swapping due to kpidash.
3. **Given** a soak run completes, **When** the recorded memory samples are plotted over time, **Then** the trend line is flat (no sustained upward slope) after the warm-up period.

---

### User Story 2 - Developer can observe and diagnose memory behavior (Priority: P2)

A developer investigating suspected leaks needs first-class visibility into kpidash's memory usage from within the application itself, without having to install extra tooling on the production Pi (which, as observed, can cause the service to restart and destroy the evidence). This includes both LVGL's internal heap accounting and process-level RSS.

**Why this priority**: The original incident was lost because the act of installing `smem` restarted the service. Built-in instrumentation makes future incidents observable without disturbing the running system, and lets soak tests produce machine-readable evidence.

**Independent Test**: Start kpidash, let it run for a few minutes, then inspect the produced memory telemetry (log lines, Redis keys, or both). Confirm samples are present at the documented cadence and contain the expected fields.

**Acceptance Scenarios**:

1. **Given** kpidash is running, **When** a developer reads the kpidash log or a designated Redis status key, **Then** they can see periodic memory samples that include at minimum the LVGL heap used / max-used / fragmentation and the process RSS.
2. **Given** a soak run has completed, **When** the developer extracts the memory telemetry, **Then** they can produce a time-series of memory usage suitable for trend analysis without re-running the test.
3. **Given** memory telemetry is being emitted, **When** kpidash is running normally, **Then** the instrumentation itself does not measurably degrade display refresh rate or add meaningful memory overhead.

---

### User Story 3 - Identified leaks are fixed and prevented from regressing (Priority: P2)

Once the leaks are localized via instrumentation and code review, the code paths responsible are corrected so that objects, styles, chart series, image/canvas buffers, and fonts are reused or properly freed across the application's lifecycle. A regression guard is added so a future change that reintroduces unbounded growth is caught before it ships.

**Why this priority**: Fixing without a guard means the same class of bug will return; guarding without fixing leaves the dashboard unusable. Both must happen, but instrumentation (Story 2) is the prerequisite that makes the fix work targeted rather than speculative.

**Independent Test**: For each suspected leak source identified in this work, exercise the corresponding code path repeatedly in a focused test and observe that LVGL heap usage and process RSS return to (or stay near) baseline.

**Acceptance Scenarios**:

1. **Given** a code path identified as leaking, **When** it is exercised N times in a row in a focused test, **Then** LVGL heap `max_used` after the run is bounded (does not scale with N).
2. **Given** the fixes have been merged, **When** the soak test from Story 1 is re-run on the target host, **Then** the steady-state envelope from SC-001 is satisfied.
3. **Given** a future change is proposed, **When** the regression guard runs in CI or against a smoke build, **Then** unbounded heap growth in the covered widgets causes the guard to fail.

---

### Edge Cases

- **Redis disconnect / reconnect storms**: When Redis is unreachable and `redis_reconnect_if_needed()` is retrying every poll tick, the per-tick allocations (error widget updates, reconnect state, log strings) must not accumulate.
- **Registry churn**: When clients appear, disappear, and reappear in the registry, the widgets allocated to display them must be reused or fully torn down — not left orphaned in the LVGL object tree.
- **Fortune / status text rotation**: Widgets that update their text on every tick must reuse the same `lv_obj_t` and not recreate labels or styles.
- **Graph / chart series**: Chart widgets that receive new datapoints every second must append to existing series, not call `lv_chart_add_series` repeatedly.
- **DRM framebuffer churn on the Pi 5**: If the underlying DRM/KMS driver retains scanout or DMA buffers that grow with LVGL object count, the leak may manifest as kernel-side memory growth even when the LVGL heap looks healthy; the investigation must distinguish these cases.
- **Long uptime overflow**: Any counters used for memory telemetry must not wrap or misreport over multi-day uptimes.
- **Loss of evidence on restart**: Any future need to install diagnostic tooling on the production host must not be the only way to gather memory data, since installing such tools has been observed to restart the service.

## Requirements *(mandatory)*

### Functional Requirements

- **FR-001**: kpidash MUST emit periodic in-process memory telemetry that includes, at minimum: LVGL heap total / used / free / max-used / fragmentation, and process resident set size (RSS).
- **FR-002**: Memory telemetry MUST be available without installing additional tools on the host — at minimum via the kpidash log file, and also exposed through Redis under the existing system-info / status mechanism so dashboards and tests can consume it.
- **FR-003**: Memory telemetry MUST be sampled at a cadence frequent enough to detect a leak within a single soak test (on the order of once per minute or faster) but infrequent enough not to dominate the log.
- **FR-004**: The project MUST include a documented soak-test procedure that runs kpidash against representative Redis traffic for a defined duration on the target hardware (or equivalent) and captures the telemetry from FR-001.
- **FR-005**: The soak-test procedure MUST produce a pass/fail verdict based on the steady-state envelope defined in SC-001, not on subjective inspection.
- **FR-006**: Each kpidash widget that updates on a recurring timer MUST reuse its underlying LVGL objects (labels, styles, chart series, image/canvas buffers, fonts) across updates rather than recreating them.
- **FR-007**: Any LVGL object created in response to a transient condition (e.g., a Redis error banner, a registry entry appearing) MUST be either reused on subsequent occurrences or explicitly deleted when no longer needed.
- **FR-008**: When the registry adds or removes clients, kpidash MUST not leave orphaned LVGL objects, styles, or buffers behind.
- **FR-009**: kpidash MUST include a focused regression test or harness that exercises the previously leaking code paths repeatedly and asserts that LVGL `max_used` is bounded independent of iteration count.
- **FR-010**: kpidash MUST log a clearly recognizable warning line **and surface a warning on the dashboard status bar** if its own LVGL heap usage crosses a compile-time high-water threshold, so the condition is visible both in `journalctl` and on-screen even without external monitoring. The threshold is a compile-time constant (rebuild to change); this project runs only on local hosts the developer controls, so runtime configurability is not required.
- **FR-011**: The investigation MUST document, in the spec folder, which specific code paths were identified as leak sources, the evidence used to identify them, and the fix applied to each.
- **FR-012**: The fix MUST not regress display refresh rate, Redis poll cadence, or startup time in any user-visible way.

### Key Entities *(include if feature involves data)*

- **Memory sample**: A single point-in-time observation of kpidash memory state. Attributes: timestamp, LVGL heap used / free / max-used / fragmentation, process RSS, uptime. Source of the time-series used to verify steady-state behavior.
- **Soak run**: A continuous execution of kpidash over a defined duration with representative input. Produces an ordered series of Memory samples and a pass/fail verdict against the steady-state envelope.
- **Leak source**: A specific code path or widget identified during the investigation as causing unbounded growth. Attributes: location, evidence (which sample series demonstrated the leak), nature of the leak (object / style / chart series / buffer / font / other), fix applied.

## Success Criteria *(mandatory)*

### Measurable Outcomes

- **SC-001**: After a 24-hour soak run on the target hardware under representative Redis traffic, kpidash process RSS at the end of the run is no more than 1.5× the steady-state value measured one hour after start.
- **SC-002**: During the same 24-hour soak run, LVGL heap `max_used` measured at the end of the run is no more than 1.2× the value measured one hour after start.
- **SC-003**: During the same 24-hour soak run, the host on which kpidash runs does not enter swap due to kpidash and is not restarted by the OOM killer or systemd because of kpidash memory usage.
- **SC-004**: Memory telemetry is available within 60 seconds of kpidash startup and continues uninterrupted for the lifetime of the process.
- **SC-005**: For each leak source identified during the investigation, the corresponding focused regression test shows bounded LVGL heap usage when its code path is exercised at least 1,000 times consecutively (no monotonic growth beyond a small constant).
- **SC-006**: A developer unfamiliar with the investigation can, by reading the documentation produced by this work, reproduce the soak test and interpret its pass/fail verdict without further guidance.

## Assumptions

- The original 7.7 GB → 430 MB drop observed on rpi53 was caused by the kpidash process itself (directly, or via tightly coupled framebuffer / DMA buffers that scale with LVGL object count), as concluded by the diagnostic conversation in `.scratch/mem-leak.md`. The investigation will validate or refute this assumption early using in-process telemetry; if the leak proves to be elsewhere (kernel cache, Redis, GPU carve-out), the scope will be revisited.
- "Representative Redis traffic" for the soak test means traffic patterns equivalent to what the production rpi53 host sees: live client check-ins, registry churn, status updates, and whatever cadence drives the graph widgets. Capturing or replaying real traffic is acceptable; a synthetic generator that produces the same shape is also acceptable.
- The target hardware for the soak test is the Raspberry Pi 5 (or the rpi53 unit specifically). Equivalent hardware may be substituted if the Pi 5 is unavailable, but the final acceptance run is on a Pi 5.
- LVGL's built-in memory monitor (`lv_mem_monitor` and friends) provides accurate-enough accounting for the LVGL heap. Enabling it (and any related `lv_conf.h` flags) is in scope for this work.
- It is acceptable to bias toward reusing LVGL objects everywhere by convention, even in widgets not yet proven to leak, as a defensive measure — provided this does not change visible behavior.
- The soak test does not need to run inside CI; it is acceptable for it to be a documented manual or scripted procedure run on the target host. A shorter smoke variant suitable for developer machines is desirable but not strictly required.
