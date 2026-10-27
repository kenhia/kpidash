# Network-health card: brainstorm record (korg:4059, program korg:4068)

2026-10-26, cleo session with Ken. Slice 1 of the program that replaces the
Fortune card with a 2×1 network-health card on rpi53.

## The pick: B · Rail

Four looks were mocked at the real cell size (1264×628, Fortune's frame):
A Fabric map (stylised topology), B Rail (one column per host under a
fabric backplane), C Reach rings (hosts as rays through fabric / gateway /
internet rings), D Pulse (verdict + hour-long heartbeat trace). Ken picked
B: "best at-a-glance for the different states." The mock-up page is
`netcard-mockups.html` beside this file (open it in a browser; the state
buttons drive all four cards; `#<state>.cardB` shows one card at 1:1).
Six state captures of B are attached to korg:4059.

## Locked layout (B)

- Header: verdict word (Montserrat bold 48, level colour) and one summary
  line (16, speed tokens coloured). Right: three tiles, UDM RTT / WAN RTT /
  UPLINK.
- Rail: 8 px bar across the card in the level colour, one tick per host
  column in that column's attention colour.
- Five columns, 226 px each: host (bold 20) + NIC (14 muted); speed numeral
  (bold 48, leg colour); subtitle (16, bold + coloured when attention); 60-min
  throughput sparkline with area fill and red drop ticks; `LFC n (+Δ)` /
  `rx err n`; UDM · PEERS · WAN pills; WAN RTT / feed age.
- Attention per column: red = stale, no link, or no path to UDM/Internet;
  yellow = on 1G backup, LFC delta, or rx errors. Attention takes the whole
  column: 10 px top band, 3 px frame, rail tick.
- Stale: hatched fill, grey band, `STALE · n s`, grey pills. Never green.
- Hosts: kubs0, kubsdb, kai, gratch (bonded) + cleo (5G single NIC, port 5;
  producer is kpidash-client-win, korg:4057).

## Palette extension (first step of the dashboard refresh)

Mocha stays. Added to `common.h` when the card lands: Teal `#94E2D5` (10G,
the healthy fabric colour, chosen so the fabric reads apart from the green
the host cards use), Sapphire `#74C7EC` (5G), Lavender `#B4BEFE`
(structure). Yellow (1G / warn) and Red (crit) are the existing WS colours.
The UPLINK tile's "1G" stays neutral: that link's normal speed, not a fallback.

## LVGL cost

Rects, labels, `lv_line`/`lv_chart` sparklines (5 × 60 points, under one
dev graph), pills as rounded rects. Fonts 14/16/20 regular and 16/20/48
bold, all already generated. Nothing animates.

## Fields (handed to korg:4060)

Per host: leg (10g|5g|1g|down), speed_mbps, bond, lfc + 24 h delta,
rx_errors + 24 h delta, reach {udm, peers, wan} probed from the host,
rtt_ms {udm, wan}, bytes_per_s, drops (last 60 min), ts. The panel derives
verdict, level, uplink state (peers ok and no host reaches UDM) and the
history. Open for slice 2: uplink load %, and producer events vs panel-side
LFC diff for `drops`.

## Side note from Ken

Fortune is displaced, not retired: it comes back once the dev graphs and
host cards are redesigned smaller (so more machines fit).
