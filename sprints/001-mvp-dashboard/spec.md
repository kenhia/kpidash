# Feature Specification: kpidash MVP — Multi-Client Status Dashboard

**Feature Branch**: `001-mvp-dashboard`
**Created**: 2026-03-30
**Status**: Tasks Generated — Ready for Implementation
**Input**: `.scratch/pre-spec-thoughts.md` + expanded by author

## User Scenarios & Testing *(mandatory)*

### User Story 1 — At-a-Glance Machine Health (Priority: P1)

The user glances at the Pi 5 dashboard and immediately sees all known client
machines displayed as cards. Each card shows whether that machine is online
(green health indicator) or has gone silent (red indicator). The card also
shows hostname and system uptime. This is the foundational view; all other
widgets build on top of a client card.

**Why this priority**: Replacing ad-hoc terminal monitoring with a
passive, always-visible health board is the core value proposition of the
dashboard. Nothing else is meaningful without this foundation.

**Independent Test**: Run the dashboard on the Pi 5 with one or more client
health-ping scripts running on other machines. The dashboard shows green
indicators for active clients and turns red within 5 seconds of a client
going silent. Delivered value: operator can confirm at a glance which
machines are alive.

**Acceptance Scenarios**:

1. **Given** the dashboard is running and a client is sending health pings,
   **When** the user looks at the screen, **Then** the client's card is
   visible with a green health indicator and the client hostname.
2. **Given** a client has been sending pings and then stops,
   **When** 5 seconds have elapsed since the last ping, **Then** the
   client's health indicator turns red.
3. **Given** the dashboard is running with no known clients,
   **When** a new client sends its first health ping, **Then** a new client
   card appears on the dashboard automatically without restart.
4. **Given** a client card is showing red (offline), **When** that client
   resumes sending pings, **Then** the indicator returns to green.
5. **Given** the dashboard loses its connection to the Redis server,
   **When** the connection is restored, **Then** client state is resumed
   from the data stored in Redis without loss.

---

### User Story 2 — System Telemetry Per Client (Priority: P2)

Each client card displays live telemetry: CPU usage, top CPU core usage, RAM
capacity and current usage, GPU type with VRAM capacity and current usage,
current GPU compute usage, and disk information (type, label, capacity, and
usage). The user can see the health of all their machines without opening a
single terminal.

**Why this priority**: This is the primary driver for building the dashboard
— eliminating the need to run `nvtop`, `htop`, and `df` across multiple
machines.

**Independent Test**: Run the Linux client telemetry daemon on one machine.
The corresponding dashboard card displays updating CPU, RAM, and GPU values
that match what `nvtop`/`htop` would show on that machine.

**Acceptance Scenarios**:

1. **Given** a client is sending telemetry, **When** the user views that
   client's card, **Then** they see CPU usage percentage, RAM used/total,
   GPU name, VRAM used/total, GPU usage percentage, and at least one disk
   entry showing type, label, capacity, and percentage used.
2. **Given** a client machine does not have a GPU, **When** its card is
   displayed, **Then** GPU fields are either hidden or shown as not
   applicable — the card does not show an error state.
3. **Given** a client has multiple disks configured for reporting,
   **When** its card is displayed, **Then** all configured disks are shown.
4. **Given** a client's CPU usage spikes to 100%, **When** the dashboard next
   refreshes, **Then** the CPU usage display reflects the spike within the
   telemetry reporting interval.

---

### User Story 3 — Activity Tracking (Priority: P3)

The dashboard displays a global Activities widget showing current and recent
activities across all clients. Any client (human via CLI command or AI agent
via MCP server) can start an activity with a name, host, and start timestamp.
Activities show elapsed time while active and transition to "done" with total
duration when ended. The user sees up to 10 recent activities at once
(this limit is configurable).

**Why this priority**: Activity awareness — knowing what a machine or agent is
working on — is the second most-valuable visibility need after health and
telemetry.

**Independent Test**: Use the Linux client CLI to start an activity on one
machine, then end it. The Activities widget shows the activity as active with
incrementing elapsed time, then transitions to done with final duration. Stands
alone without full telemetry client.

**Acceptance Scenarios**:

1. **Given** a client starts an activity, **When** the dashboard refreshes,
   **Then** the activity appears in the Activities widget with host name,
   activity name, and elapsed time (counting up).
2. **Given** an active activity, **When** the client sends an activity-end
   update, **Then** the activity transitions to "done" state and displays
   the total elapsed duration without a running timer.
3. **Given** more than 10 activities are tracked (active + recent done),
   **When** a new activity starts, **Then** the oldest completed entry is
   removed to keep the list at most 10 items (or the configured maximum).
4. **Given** a client goes offline while an activity is active, **When** some
   time passes, **Then** the activity remains in the list in its last known
   state (active) until explicitly ended or manually cleared.
5. **Given** an AI agent starts an activity via the MCP server, **When** the
   dashboard refreshes, **Then** the activity appears identically to a
   human-initiated one.

---

### User Story 4 — Repo Status Awareness (Priority: P4)

The dashboard displays a Repo Status widget listing git repositories that have
uncommitted changes or are on a non-default branch. The client periodically
scans a configured set of watched repositories and pushes status updates. The
user can see at a glance whether any work-in-progress exists across their
machines without opening a terminal.

**Why this priority**: Prevents forgotten branches and uncommitted work from
building up across multiple machines.

**Independent Test**: Configure the Linux client to watch one git repository,
switch it to a feature branch, then run the client. The Repo Status widget
shows that repo with its current branch. Then commit and switch back to main;
repo status disappears from the widget.

**Acceptance Scenarios**:

1. **Given** a watched repo has uncommitted changes, **When** the client sends
   a status update, **Then** the repo appears in the widget with host and an
   indicator that changes are present.
2. **Given** a watched repo is on a non-default branch, **When** the client
   sends a status update, **Then** the repo appears in the widget with the
   current branch name.
3. **Given** a watched repo is on the default branch with no uncommitted
   changes, **When** the client sends a status update, **Then** the repo does
   NOT appear in the widget (clean repos are not listed).
4. **Given** the Repo Status widget has no items to show, **When** the
   dashboard displays, **Then** the widget is still visible but shows an empty
   or "all clean" state.

---

### User Story 5 — Linux Client Setup (Priority: P5)

A developer sets up a new Linux machine to report to the dashboard. They
install the client, configure the Redis connection (host and auth), and launch
the daemon. The daemon automatically begins sending health pings and system
telemetry without further configuration. For setting activities, a CLI command
is available.

**Why this priority**: The dashboard is only as useful as the number of
machines reporting to it. Linux clients are the primary target.

**Independent Test**: Install the client on a fresh Linux machine, point it at
the Redis server, and start the daemon. Within 10 seconds, a new card appears
on the dashboard with health and telemetry.

**Acceptance Scenarios**:

1. **Given** the client is installed and the Redis connection is configured,
   **When** the daemon starts, **Then** health pings and telemetry are sent
   automatically with no additional user action.
2. **Given** the daemon is running, **When** the user runs the activity-start
   CLI command with an activity name, **Then** the activity appears in the
   dashboard Activities widget.
3. **Given** the daemon is running, **When** the user runs the activity-done
   CLI command, **Then** the current activity transitions to done on the
   dashboard.
4. **Given** the client daemon encounters a transient Redis connection failure,
   **When** Redis becomes reachable again, **Then** the daemon reconnects and
   resumes reporting automatically.

---

### User Story 6 — Windows Client Setup (Priority: P6)

A developer sets up a Windows machine to report to the dashboard. They install
the Windows client, configure the Redis connection, and start the background
process. The Windows client reports health, telemetry, and can receive activity
commands from the user.

**Why this priority**: Important for complete coverage of the user's machine
fleet, but lower priority than the Linux client.

**Independent Test**: Install the Windows client on a Windows machine, configure
Redis connection, start the background process. A new card appears on the
dashboard within 10 seconds.

**Acceptance Scenarios**:

1. **Given** the Windows client is installed and configured, **When** the
   background process starts, **Then** the machine appears on the dashboard
   with health and telemetry within 10 seconds.
2. **Given** the Windows client is running, **When** the user runs the
   activity CLI command, **Then** the activity appears in the dashboard
   Activities widget.
3. **Given** the Windows client is configured to watch repos, **When** a repo
   is on a non-default branch, **Then** the repo appears in the dashboard
   Repo Status widget.

---

### User Story 7 — Agent Activity Reporting via MCP Server (Priority: P7)

An AI agent (e.g., GitHub Copilot, Claude) uses the MCP server to report its
current activity to the dashboard. The operator can see what an AI agent is
working on without context-switching away from the dashboard.

**Why this priority**: Nice-to-have for AI workflow visibility; lower priority
than human client setup.

**Independent Test**: Configure an AI agent to use the MCP server. Invoke the
agent on a task. The dashboard Activities widget shows the agent's activity
with elapsed time.

**Acceptance Scenarios**:

1. **Given** the MCP server is running and an agent starts a task,
   **When** the agent calls the activity-start tool, **Then** the activity
   appears in the dashboard Activities widget with the agent's identified
   host or name and elapsed time.
2. **Given** an agent's activity is active, **When** the agent calls the
   activity-done tool, **Then** the activity transitions to done on the
   dashboard.

---

### User Story 8 — Dashboard Status and Log Access (Priority: P8)

When the dashboard encounters a warning or error condition (e.g., a widget
failure, a configuration problem, or an internal fault), a status bar appears
at the bottom of the screen showing the message. Because the dashboard Pi has
no keyboard or mouse, a client operator can acknowledge the issue remotely
using a CLI command. Once acknowledged, the status bar disappears until the
next warning or error occurs. For deeper investigation, the dashboard publishes
its log file path to Redis so any client can retrieve it and use `scp` to copy
the log for inspection.

**Why this priority**: Operational visibility is important but only surfaces
when something goes wrong. It does not affect day-to-day display and can be
added after the primary widgets are working.

**Independent Test**: Trigger a known warning condition (e.g., a misconfigured
widget). Confirm the status bar appears at the bottom of the screen. Run the
client CLI acknowledge command. Confirm the status bar disappears. Separately,
run the client CLI log-path command and confirm it returns a valid path.

**Acceptance Scenarios**:

1. **Given** the dashboard encounters a warning or error, **When** the event
   occurs, **Then** a status bar appears at the bottom of the screen showing
   the severity and message text.
2. **Given** the status bar is visible, **When** an operator runs the client
   CLI acknowledge command, **Then** the status bar disappears from the
   dashboard display.
3. **Given** the status bar has been acknowledged and another warning or error
   occurs, **When** the new event is recorded, **Then** the status bar
   reappears with the new message.
4. **Given** the dashboard is running, **When** an operator runs the client
   CLI log-path command on any machine, **Then** they receive the full path
   to the dashboard log file on the Pi and can use `scp` to copy it.
5. **Given** there are no active warnings or errors, **When** the dashboard
   displays, **Then** the status bar is hidden and does not occupy any visible
   screen space.

---

### User Story 9 — Fortune Widget (Priority: P9)

The dashboard displays a Fortune widget showing rotating quotes, facts, or
other text content. Content can come from the Pi itself or be pushed by a
client. The widget provides ambient interest to the always-visible display.

**Why this priority**: Lowest priority — aesthetic enhancement only.

**Independent Test**: Configure the Fortune widget. It displays rotating text
content on the dashboard without affecting other widgets.

**Acceptance Scenarios**:

1. **Given** the Fortune widget is enabled, **When** the dashboard is
   displayed, **Then** a fortune or quote is visible and updates periodically.
2. **Given** a client pushes a "special fortune" message, **When** the
   dashboard receives it, **Then** the pushed content is shown in the Fortune
   widget immediately — without waiting for the current fortune to finish —
   and remains displayed for a configurable duration (default 5 minutes).

---

### Edge Cases

- **Client with no GPU**: GPU widget fields are gracefully hidden or marked
  "N/A"; dashboard does not display error state.
- **Client with multiple disks**: All configured disks are shown within the
  client card; layout handles variable counts.
- **Dashboard starts before Redis is available**: Dashboard waits and retries
  Redis connection, displaying a "connecting" state until ready.
- **Redis connection drops while dashboard is running**: Dashboard retains
  last-known state for all clients; clearly indicates connection loss;
  reconnects automatically when Redis is available again.
- **Max client cards reached**: When the number of actively-reporting clients
  reaches the configured maximum, non-priority clients that have been silent
  the longest are excluded from display to make room. Priority clients are
  never displaced.
- **No pointer/keyboard input**: The dashboard Pi has no mouse or keyboard.
  No UI element that requires pointer or keyboard interaction (e.g., scroll
  bars) may be used anywhere in the dashboard.
- **Duplicate hostname**: If two machines report with the same hostname, their
  data merges into one card or a conflict indicator is shown.
- **Activity started on a client with no daemon**: The dashboard receives only
  the activity update (via CLI or MCP) and shows the activity without
  associated telemetry.
- **Stale "done" activities**: Done activities that are old enough fall off
  the Activities list per the configured retention policy.
- **Multiple simultaneous status messages**: If more than one warning or error
  is active at the same time, the status bar displays the most recently
  recorded one. Acknowledging it clears it; if others remain unacknowledged,
  the next most recent is shown.
- **Status message at startup**: If unacknowledged status messages from a
  previous run are stored in Redis, the dashboard restores and displays
  them on restart.

---

## Requirements *(mandatory)*

### Functional Requirements

#### Dashboard Display

- **FR-001**: The dashboard MUST run fullscreen on the Raspberry Pi 5's
  connected display without requiring a desktop environment (X11/Wayland).
- **FR-002**: The dashboard MUST maintain a visual refresh on screen at
  approximately 30 frames per second.
- **FR-002a**: The dashboard MUST NOT display a title bar, application chrome,
  window decorations, or any persistent header identifying the application.
  The entire screen area is devoted to dashboard content.
- **FR-003**: The dashboard MUST add a new client card automatically the first
  time a client sends any message via Redis, provided the number of displayed
  cards is below the configured maximum, without requiring a restart.
- **FR-004**: The dashboard MUST persist client state in Redis so that a
  dashboard restart recovers all last-known client telemetry and activities.
- **FR-005**: The dashboard MUST display a clear visual indication when its
  Redis connection is unavailable.
- **FR-006**: The dashboard MUST support a configurable maximum number of
  simultaneously displayed client cards.
- **FR-007**: The dashboard MUST support a configurable "priority clients" list
  that specifies which clients are always displayed and in what order. Priority
  clients MUST never be displaced by non-priority clients regardless of
  activity level.
- **FR-008**: The dashboard MUST NOT use any UI element that requires pointer
  or keyboard interaction (e.g., scroll bars, tooltips, dropdowns). All
  displayed information must be visible without user input.
- **FR-009**: When the number of reporting clients reaches the configured
  maximum, non-priority clients that have been silent the longest MUST be
  excluded from display to keep the visible count at or below the maximum.

#### Health Widget

- **FR-010**: Each client card MUST display a health indicator (green/red)
  showing whether the client has pinged within the last 5 seconds.
- **FR-011**: Clients MUST send a health ping to the Redis server at least
  once every 3 seconds while alive.
- **FR-012**: The client card MUST display the client's hostname.
- **FR-013**: The client card SHOULD display the client's system uptime when
  provided in the health ping.

#### Telemetry Widgets (per client card)

- **FR-020**: Each client card MUST display current CPU usage as a percentage.
- **FR-021**: Each client card MUST display the usage percentage of the
  single most-loaded CPU core.
- **FR-022**: Each client card MUST display system RAM: total capacity and
  current usage amount.
- **FR-023**: For clients that report GPU data, the client card MUST display
  GPU brand and model name, VRAM total capacity, VRAM current usage, and
  current GPU compute usage percentage.
- **FR-024**: For clients without GPU data, GPU fields MUST be hidden or
  explicitly shown as not applicable — no error state.
- **FR-025**: Each client card MUST display configured disk entries, each
  showing: storage type (NVMe, SSD, HDD, or other), a user-defined label or
  path, total capacity, and current usage.
- **FR-026**: Clients MUST report telemetry at a configurable interval,
  defaulting to once every 5 seconds.

#### Activities Widget

- **FR-030**: The dashboard MUST display a global Activities widget showing
  up to 10 activities (active and recently completed) by default; this
  maximum MUST be configurable.
- **FR-031**: Each activity entry MUST show: host or agent name, activity
  name/description, current state (active or done), and time (elapsed for
  active, total duration for done).
- **FR-032**: Active activities MUST display a live elapsed timer that
  increments on the dashboard display.
- **FR-033**: Any client (via daemon, CLI, or MCP server) MUST be able to
  start an activity, providing an activity name and the starting timestamp.
- **FR-034**: Any client MUST be able to end an activity, providing an end
  timestamp.
- **FR-035**: When more than the configured maximum (default: 10) activities
  exist, the oldest completed activities MUST be removed from the visible
  list to stay at or below the maximum.

#### Repo Status Widget

- **FR-040**: The dashboard MUST display a Repo Status widget listing watched
  git repositories that are on a non-default branch or have uncommitted changes.
- **FR-041**: Repositories on the default branch with no uncommitted changes
  MUST NOT appear in the Repo Status widget.
- **FR-042**: Each repo entry MUST show: the host it belongs to, repository
  name, current branch, and whether uncommitted changes are present.
- **FR-043**: Clients MUST accept a three-tier repository configuration:
  (1) an explicit list of individual repository paths to always watch;
  (2) a list of root directories to scan for any `.git` repository up to a
  configurable depth; and (3) a list of paths to exclude from scanning.
  Clients MUST scan and report status changes periodically.

#### Fortune Widget

- **FR-050**: The dashboard MUST display a Fortune widget that shows output
  from the `fortune` command installed on the Pi 5. The `fortune` package
  MUST be present on the Pi 5; the widget MUST NOT start if `fortune` is
  unavailable.
- **FR-051**: The Fortune widget MUST update its content periodically at a
  configurable interval, defaulting to 5 minutes.
- **FR-052**: Any client or the MCP server MUST be able to push custom
  fortune content to the widget. Pushed content MUST be displayed
  immediately, interrupting the current rotation, and remain visible for a
  configurable duration (default 5 minutes) before returning to the normal
  rotation.

#### Redis Communication

- **FR-060**: All dashboard–client communication MUST use a Redis server
  running on the Raspberry Pi 5.
- **FR-061**: Redis authentication MUST be supported via the `REDISCLI_AUTH`
  environment variable on all components (dashboard and all clients).
- **FR-062**: The dashboard and all client applications MUST handle Redis
  connection loss gracefully and reconnect automatically without requiring
  a process restart.
- **FR-063**: The Redis communication schema (key naming, channel names,
  message formats) MUST be formally documented in `docs/CLIENT-PROTOCOL.md`
  as the canonical protocol reference.

#### Linux Client

- **FR-070**: The Linux client MUST provide an autostart daemon that sends
  health pings and telemetry automatically on startup.
- **FR-071**: The Linux client MUST provide a CLI command to start an activity
  with a name argument.
- **FR-072**: The Linux client MUST provide a CLI command to end the current
  activity.
- **FR-073**: The Linux client MUST accept a configuration file or environment
  variable specifying which disks to report and which repo paths to watch.
- **FR-074**: The Linux client daemon MUST reconnect to Redis automatically
  after a connection failure without user intervention.

#### Windows Client

- **FR-080**: The Windows client MUST provide a process that sends health
  pings and telemetry. Running as a persistent background process is not
  required for MVP; the process may be started manually. Transition to a
  managed Windows Service is deferred post-MVP.
- **FR-081**: The Windows client MUST provide a CLI command to start and end
  activities.
- **FR-082**: The Windows client MUST support the same disk and repo
  configuration as the Linux client.

#### MCP Server

- **FR-090**: An MCP server MUST be provided that exposes at minimum two
  tools: start-activity and end-activity.
- **FR-091**: The MCP server MUST connect to Redis using the same
  authentication mechanism as the clients.
- **FR-092**: Activities started via MCP MUST appear in the dashboard's
  Activities widget identically to client-initiated activities.

#### Dashboard Status Widget

- **FR-100**: The dashboard MUST display a status bar at the bottom of the
  screen when one or more unacknowledged warning or error messages exist.
  The status bar MUST be hidden when no unacknowledged messages are present.
- **FR-101**: Each status message MUST include: severity level (warning or
  error), a human-readable description, and the timestamp it was recorded.
- **FR-102**: When multiple unacknowledged messages exist simultaneously, the
  status bar MUST display the most recently recorded one.
- **FR-103**: Any client application MUST provide a CLI command to acknowledge
  the current status message. Acknowledging via Redis causes the dashboard
  to remove that message from display; if other unacknowledged messages
  remain, the next most recent is shown.
- **FR-104**: The dashboard MUST write the full path of its log file to Redis
  on startup and keep that value current.
- **FR-105**: All client applications MUST provide a CLI command to read the
  dashboard log file path from Redis and print it to stdout, allowing the
  operator to subsequently copy the log using `scp` or equivalent.

### Key Entities

- **Client**: A machine or agent reporting to the dashboard. Identified by
  hostname. Carries health status, telemetry, and a collection of active/
  recent activities.
- **HealthRecord**: The most recent health signal from a client, including
  timestamp and optional uptime. Determines the online/offline indicator.
- **TelemetrySnapshot**: A point-in-time reading of a client's CPU, RAM, GPU,
  and disk metrics.
- **DiskEntry**: One disk's telemetry: storage type, label/path, capacity,
  and usage. A client may have multiple disk entries.
- **Activity**: A named unit of work with a host, start timestamp, optional
  end timestamp, and state (active or done). Not 1:1 with a client card —
  activities live in the global Activities widget.
- **RepoStatus**: The current git state of a watched repository: host, path
  or name, current branch, and presence of uncommitted changes.
- **FortuneEntry**: A piece of text content for the Fortune widget, either
  from a local source or pushed by a client, with an optional display duration.
- **StatusMessage**: An operational warning or error recorded by the dashboard,
  carrying severity, description, timestamp, and acknowledged state. Persisted
  in Redis so it survives a dashboard restart.

---

## Success Criteria *(mandatory)*

### Measurable Outcomes

- **SC-001**: Within 10 seconds of a new client starting its daemon, its card
  appears on the dashboard with live health and telemetry data — no dashboard
  restart required.
- **SC-002**: A client going offline (stopping its daemon) is reflected by a
  red health indicator within 5 seconds on the dashboard.
- **SC-003**: Telemetry values displayed on the dashboard (CPU, RAM, GPU) are
  no more than 10 seconds stale under normal operating conditions.
- **SC-004**: Starting an activity via the Linux client CLI results in it
  appearing in the Activities widget within 3 seconds.
- **SC-005**: Setting up the Linux client on a new machine (install + configure
  + start) takes under 5 minutes with access to the README.
- **SC-006**: The dashboard remains responsive (no visible freeze or flicker)
  with at least 8 client machines actively reporting.
- **SC-007**: After a Redis connection outage and restore, the dashboard
  recovers to full operating state with correct client data within 15 seconds
  of Redis becoming reachable — without operator intervention.
- **SC-008**: The dashboard runs continuously for at least 72 hours without
  requiring a restart, memory exhaustion, or visible degradation.

---

## Assumptions

- The Raspberry Pi 5 already has a Redis server installed and running. The
  dashboard does not install or manage Redis.
- All machines (Pi 5 and clients) are on the same local area network with
  reliable connectivity.
- `REDISCLI_AUTH` is set as an environment variable on each host that runs a
  dashboard or client component. No other authentication mechanism is required
  for MVP.
- The GPU telemetry for Linux clients targets NVIDIA GPUs only for MVP.
  AMD and Intel GPU support is explicitly out of scope for MVP.
- A client can report at most one GPU for MVP. Multi-GPU reporting is
  out of scope.
- The dashboard layout is designed for the 3840×2160 display connected to the
  Pi 5. It is not required to be resolution-agnostic for MVP.
- The Linux client daemon is distributed as source and built natively on the
  target machine (no pre-built binaries for MVP).
- The Windows client is a separate script or binary that is started manually
  for MVP. Running as a persistent background process or Windows Service is
  not required for MVP.
- The kpidash dashboard binary is cross-compiled on an x86_64 workstation and
  deployed to the Pi 5, per the existing cross-compilation workflow.
- The POC codebase in this repo (UDP-based, single `src/` directory) is
  treated as reference only and will be substantially refactored or replaced.
  No backwards compatibility with the POC protocol is required.
- Activities are not persisted across a full Redis flush. Redis is the source
  of truth; if Redis data is cleared, activity history is lost.
- The `fortune` package is installed on the Pi 5 and is a required dependency
  for the Fortune widget. The dashboard will not display the Fortune widget if
  `fortune` is not installed.

---

## Resolved Clarifications

All open questions from [`specs/001-mvp-dashboard/clarifications.md`](clarifications.md)
have been answered and incorporated into the requirements above. No open
questions remain.

