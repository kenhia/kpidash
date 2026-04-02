# Clarifications Needed: kpidash MVP

**Branch**: `001-mvp-dashboard`
**Created**: 2026-03-30
**Status**: ✅ All answered — incorporated into spec.md

These questions emerged while writing the spec. Answering them will remove
ambiguity from the functional requirements and allow planning to proceed
without guesswork.

---

## Q1 — Activities Widget: Maximum Visible Count (N)

**Relevant requirement**: FR-030

**Context**: The spec says the Activities widget shows "up to N activities."
The value of N determines the layout footprint of the widget and how history
is managed.

**What we need to know**: What is the maximum number of activity entries
(active + recently done) the Activities widget should display at once?

| Option | N   | Implications |
|--------|-----|--------------|
| A      | 5   | Compact widget; shorter history visible at once |
| B      | 10  | Moderate size; good balance of history and screen space |
| C      | 15  | Larger widget; more history at the cost of screen real estate |
| Custom | —   | Specify any number you prefer |

**Your answer**: B, we have plenty of screen real estate. Should be a value that we can tweak later if testing shows it needs to shrink/grow

---

## Q2 — Repo Status: How Repos Are Discovered

**Relevant requirement**: FR-043

**Context**: The spec says clients accept "a configurable list of repository
paths to watch." There are two common patterns for this.

**What we need to know**: How should a client determine which repos to report?

| Option | Approach | Implications |
|--------|----------|--------------|
| A | Explicit list in a config file (e.g., `~/.kpidash/repos.conf`) | User controls exactly which repos appear; no surprises; requires setup per machine |
| B | Auto-scan: any `.git` directory found under `$HOME` (up to some depth) | Zero config; may surface repos the user doesn't care about |
| C | Both: auto-scan with an optional exclude/include override list | Most flexible; slightly more complex config |

**Your answer**: C; config with explicit repos to check, list of root dirs to scan, list of dirs to exclude

---

## Q3 — Fortune Widget: Content Source

**Relevant requirement**: FR-050, FR-051, FR-052

**Context**: The pre-spec-thoughts noted that `fortune` isn't currently
installed on the Pi and the user may want clients to push "special" fortunes.

**What we need to know**: What should the Fortune widget do when the `fortune`
command is not installed on the Pi?

| Option | Behavior | Implications |
|--------|----------|--------------|
| A | Show a static placeholder message ("No fortune daemon available") until a client pushes one | Simple; no installation required |
| B | Require `fortune` to be installed on the Pi; dashboard fails to start the widget if it's missing | Clean but adds a Pi setup dependency |
| C | Ship a small built-in quote/fact list in the dashboard that is used as the fallback | No external dependency; always works |

**Your answer**: B; note I've installed `fortune` on the Pi now; client apps should still be allowed to "push" a fortune. Fortune widget should display a fortune for a decent amount of time (default 5 minutes (?)). Pushed fortune should be displayed without waiting for previous fortune to time out.

---

## Q4 — Dashboard Layout with Many Client Cards

**Relevant requirement**: FR-001, FR-003

**Context**: The spec auto-creates client cards when new clients connect.
On a 4K display there is a lot of room, but with many machines the layout
must have a defined overflow behavior.

**What we need to know**: When the number of client cards exceeds what fits
on-screen in the default grid layout, what should happen?

| Option | Behavior | Implications |
|--------|----------|--------------|
| A | Scroll: the client card area becomes vertically scrollable | All clients always visible via scroll; no client is lost |
| B | Paginate: cards are split across pages, advancing automatically on a timer | Clean full-screen look; clients off the current page are hidden until next rotation |
| C | Shrink: cards scale down to fit all clients on one screen up to a max count | Always one-glance view; very small cards at high client counts |
| D | Fixed max (e.g., 8); oldest-silent clients are hidden when at capacity | Predictable layout; may hide idle clients |

**Your answer**: Fixed max. The Pi should be configurable with "priority clients" that are not removed and are displayed in order (i.e. I can always look at the same places on the dashboard to see `kubs0` and `cleo`). Additional note as "scroll" was mentioned - the dashboard Pi will not have a mouse or keyboard, so we should not have anything that scrolls. If something doesn't fit, better to not show it than to use space to have a scrollbar that cannot be used.

---

## Q5 — GPU Telemetry: Vendor Support on Linux

**Relevant requirement**: FR-023

**Context**: The assumption in the spec defaults to NVIDIA GPUs. However, if
any of your Linux clients have AMD or Intel GPUs that you want reported, the
assumption needs adjusting.

**What we need to know**: Do any of your Linux client machines have non-NVIDIA
GPUs that you want to see telemetry for in the MVP?

| Option | Scope | Implications |
|--------|-------|--------------|
| A | NVIDIA only (keep current assumption) | `nvidia-smi` based; simplest implementation |
| B | NVIDIA + AMD | Requires `rocm-smi` or `radeontop` integration |
| C | All vendors best-effort (NVIDIA/AMD/Intel iGPU) | Most flexible; more work; may be unreliable for Intel iGPU |

**Your answer**: A for now. The GPU that I am interested in is an NVIDIA. May extend this at a later point, but won't be part of the MVP.

---

## Q6 — Windows Client: Background Process Mechanism

**Relevant requirement**: FR-080

**Context**: The assumption in the spec documents the Windows client as a
background process started manually or via the startup folder — not a full
Windows Service.

**What we need to know**: Is a simple background process acceptable for MVP,
or do you want it to run as a proper Windows Service (survives logoff,
managed via `services.msc`)?

| Option | Mechanism | Implications |
|--------|-----------|--------------|
| A | Simple background process / startup folder (keep current assumption) | Easier to implement and distribute; requires login to run |
| B | Proper Windows Service | Runs without login; survives reboot automatically; more installation complexity |

**Your answer**: A for MVP (doesn't even *need* to be background for MVP). Will devote time later to transition to "B"
