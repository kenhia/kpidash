# Sprint 023: kimac on the panel, with kpidash-client under launchd

Proposal korg:3145, covering WI 3132. This is slice 8 of program korg:3148 ("kimac:
the fleet's first Mac, fully onboarded"), the second of three monitoring legs:
kmon korg:3144, then this one, then k-homelab korg:3143. It ran as karc leg
`kpidash-df589b` on kai, headless, under `/overseen-sprint`. Every kimac probe ran
**from kai**, over `ssh -o BatchMode=yes kimac`, piped through `bash -s`.

Read before starting:
- the shared brief, handoff korg:3173;
- kmon's pre-ship handoff, korg:3177, which set the launchd pattern this follows
  exactly: `net.kenhia.<app>.<role>`, a templated plist, `gui/$(id -u)`,
  `~/Library/Logs/<app>/`, and uv's Python;
- the overseer's comment 2970, which keeps WI 2944 separate from this slice.

## Premise check

| claim (WI 3132) | verdict |
|---|---|
| kimac is not in `kpidash:*` | **held** until this sprint's test, which SADDed it |
| the client's collection is psutil and should mostly port | **holds**, verified per metric on kimac: CPU, RAM, uptime and disk all correct; GPU `null`, as on any host without NVIDIA; no temperatures are collected on any platform |
| Linux-only fields must be omitted, not zeroed | **holds as built**: `gpu` is `null` (an existing, rendered case), and disk type comes from config off Linux |
| the secrets route needs settling first | **settled upstream**: `/etc/khomelab/secrets.env` is `root:khomelab 0640` on kimac, and kmon proved a launchd-spawned job reads it |
| uv's Python, not Apple's 3.9.6 | **holds**: the scratch venv built on uv-managed CPython 3.13 |
| the panel's staleness handling renders a sleeping Mac as not-a-fault | **false as found**: a dead card is a red dot reading `offline` (screenshot below). That is the fix this sprint makes |

## Decisions

### The launcher is inline in the plist, and the tests run those exact bytes

launchd has no `EnvironmentFile=`. The plist's `ProgramArguments` is
`/bin/sh -c '<script>' kpidash-client-launchd /etc/khomelab/secrets.env <exec> daemon start --foreground`.
The script extracts `REDISCLI_AUTH`, using the same `sed` as `scripts/kpidash-auth.sh`
(`KEY='value'` or bare, never eval'd), and `exec`s the rest.

The secrets path is an **argument**, not text inside the script. That lets
`tests/test_launchd.py` render the plist, take the script string out of it, and run
it against a temporary file. The test exercises what launchd runs, not a copy of it.
The inline form also means one installed file instead of a plist plus a launcher
script that could drift apart.

With no value, it exits 78 and never execs. This is the twin of the unit's
un-prefixed `EnvironmentFile=`.

### A per-user LaunchAgent, and why sprint 021's rule does not forbid it

Sprint 021 made every unit a system unit. The reason was that a lingering
`systemd --user` manager keeps stale groups. launchd resolves groups per spawn, and
kmon measured that. A LaunchDaemon would only add root. CLAUDE.md now names the
exception and gives the reason.

### Asleep, not offline: a declared field, not an inference

The dashboard cannot tell a sleeping Mac from a dead server by silence alone. Only
the host knows it is meant to sleep. So the host declares it:
- `[client] availability = "intermittent"` in `config.toml`, mirroring the
  `availability:` value k-homelab inventory already carries for kimac;
- which becomes `"availability": "intermittent"` in the health payload.

The dashboard keeps the last value after the key expires. An offline intermittent
host is drawn grey `asleep`; an offline host of any other kind is still red `offline`.

Three details, each chosen deliberately:
- **The default is omitted from the payload.** Every existing host's bytes are
  unchanged, which is the same move kmon made with `narrow`.
- **Unknown values are errors or absence, never intermittent.** The client refuses
  them in `config.toml`, and the dashboard treats them as absent. A typo therefore
  shows an outage instead of hiding it.
- **The flag is dashboard memory only.** After a dashboard restart, a Mac that is
  asleep shows red until it next publishes. Persisting it would be a second state
  file for a small edge case. The limitation is written in `CLIENT-PROTOCOL.md`.

This is an optional field on `kpidash:client:{host}:health`. kdashdata registers that
key's schema with `additionalProperties: true`, so the field is legal there without
being listed. Whether kdashdata should *list* it, and whether kdeskdash should honour
it, is kdashdata's call. That question went to the overseer, not into the other repo.

### `os_name` on macOS

`platform.release()` on a Mac is the Darwin kernel version (`25.0.0`). Nobody reading
the panel would connect that to macOS 27. The client now reports
`platform.mac_ver()` as `macOS 27.0`. An empty `mac_ver()` falls back to the kernel
string rather than guessing.

## Triggers fired (no soak)

All on 2026-09-23, times in PDT. The agent ran from a scratch copy,
`~/.cache/kpidash-sprint23`, using a temporary `config.toml` with
`availability = "intermittent"` and the `data` disk.

1. **Collection by hand.** Output: `macOS 27.0`, CPU 0.8/2.9 %, RAM 3585/8192 MB,
   GPU `None`, `data` 39.9/460.4 GB, uptime 79.8 h.
2. **By launchd.** `launchd/install.sh install` from the scratch copy gave
   `state = running`, `runs = 1`. From kai, rpi53's Redis then held
   `kpidash:client:kimac:health` with `os_name: "macOS 27.0"` and
   `availability: "intermittent"`, plus live telemetry. **On the glass**
   (`krpidss`, 23:11): a kimac card with `macOS 27.0`, `up 3d 7h`, `3.7 GB / 8.0 GB`,
   and the `data` bar.
3. **Sleep.** `sudo pmset sleepnow` at 23:11:31, with kai touching nothing on kimac
   afterwards. Health TTL read `-2` by 23:11:42 and telemetry by 23:11:52. The panel,
   still running the *deployed* dashboard, showed a red dot and `offline`. That is
   the before picture for the asleep rendering.
4. **Wake.** An ssh at 23:12:23 woke it. Health was back by 23:12:28, from the **same
   PID** (42052) with `runs = 1`. The process resumed; launchd did not restart it.
   No intervention was needed.
5. **Bootout.** `install.sh uninstall` stopped the job, and the health key expired
   within 6 s.

**Found by trigger 5, and repaired:** `launchctl bootout` returns while the job is
still exiting. `print` answered rc 0 with the process alive straight afterwards, and a
bootstrap in that window fails. The installer now waits for rc 113 (up to 30 s)
after every bootout. A re-run confirmed the whole cycle:
- install;
- the same install again reports "unchanged";
- a changed install re-loads, with a new PID and `runs = 1`;
- uninstall gives rc 113 and no process;
- a second uninstall reports "not loaded".

**Found by trigger 4:** a Mac slept with `pmset sleepnow` answers network traffic with
a **DarkWake** and goes back to sleep. The ssh that proved recovery was followed, about
20 s later, by `No route to host`. `caffeinate -u -t 2` declared user activity and
brought kimac back to full wake (`DarkWake to FullWake … HID Activity`, 23:13:01).
Ken's caffeinate PID 18590 was not touched at any point.

Then everything was torn down: the agent was booted out, and the plist, scratch copy,
temporary config and log were removed. `~/Library/LaunchAgents` on kimac is empty.
**Nothing kpidash-related is installed on kimac now.** It remains a member of
`kpidash:clients` and of rpi53's admitted file, so its card stays up, offline, until
it publishes again.

## What shipped

- `clients/kpidash-client/launchd/`: the plist template, `install.sh`
  (`install|status|uninstall|render`, refusing everything except `render` off macOS,
  bash 3.2-clean), and its README.
- The client, now **1.2.0**: `[client] availability`, the `availability` field in the
  health payload, and `macOS <version>` as `os_name`.
- The dashboard: `client_info_t.intermittent`, `client_presence()`, and the card's
  grey `asleep`.
- `just check-units` lints the plist: it must name the secrets file, name no retired
  copy, and have no `EnvironmentVariables`.
- Docs: `CLIENT-PROTOCOL.md` §2, the client README's macOS and "Hosts that sleep"
  sections, and CLAUDE.md.

Gates: `just check` green, including 12 ctest targets and 103 client tests.
`just check-warnings-full` green.

## For the k-homelab leg (korg:3143)

- **Label** `net.kenhia.kpidash.client`. **Plist**
  `~/Library/LaunchAgents/net.kenhia.kpidash.client.plist`. **Log**
  `~/Library/Logs/kpidash/client.log`. **Assert** with
  `launchctl print gui/<uid>/net.kenhia.kpidash.client`: rc 0 means loaded, 113 means
  absent.
- **How absence is graded:** declared. With `availability = "intermittent"` in
  kimac's `config.toml`, rendered from inventory's `availability`, a silent kimac is
  grey `asleep`. Without it, the card is red `offline`.
- **config.toml for kimac:**
  - `[client] availability = "intermittent"`;
  - one disk: `path = "/System/Volumes/Data"`, `label = "data"`, `type = "ssd"`.
    `/` is the sealed system volume;
  - no `[redis]` (khlenv);
  - no Linux-only fields.
- **kimac has no `/etc/khlenv/endpoint`.** The client logged khlenv's fallback to its
  compiled-in default. That works, but it is the gap khlenv's own message names.
- **Delivery:**
  1. `kpkg`/`uv tool install kpidash-client` (1.2.0, once published) into
     `~/.local/bin` on uv's Python;
  2. then `launchd/install.sh install` from a checkout, or the render-and-deliver
     route in `launchd/README.md` if kimac still has no clone.

## Repaired in passing

- `launchd/install.sh` raced `bootout`: a same-day finding by trigger, fixed in the
  new file and re-proved on kimac. It is listed here because it was a defect found by
  testing, not a planned feature.

## Cross-repo changes made

None.
