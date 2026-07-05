# ieee802154_scan

On-demand IEEE 802.15.4 survey for the XIAO ESP32-C5 — the radio under
**Zigbee, Thread and Matter-over-Thread**, an IoT ecosystem invisible to
WiFi and BLE scans.

The survey sweeps all 16 channels (11–26) and measures **energy** on
each — an activity map that shows *where* 802.15.4 traffic is (strong
energy on channels 15/20/25 points at a nearby Zigbee/Thread network).
This is the WiFi-safe mode: it coexists with the dashboard connection.

Enumerating the actual **networks (PAN IDs)** and **device addresses**
requires promiscuous frame capture, which parks the shared 2.4 GHz radio
away from WiFi long enough to drop the dashboard — so it is deferred to a
future opt-in "deep capture" mode rather than the live survey. (The
capture/parse code is present but dormant.)

## Heap & radio model

The 802.15.4 radio is brought up only for the survey and torn down after
(`Ieee802154Service`), so it costs RAM only while scanning. Energy detect
tunes the shared radio away from WiFi only microseconds at a time, so the
survey coexists with the dashboard connection.

## Usage

- **Dashboard:** Modules → `ieee802154_scan` → Controls → **Survey now**.
  Click a channel row to expand its networks, devices and frame types.
- **REST:** `GET /api/v1/ieee802154/scan?ms=120` (ms = per-channel dwell)

## Config (`ieee802154_scan` namespace)

| Key | Default | Meaning |
|---|---|---|
| `auto_start` | `false` | Survey in the background at boot |
| `period_ms` | `0` | Background survey interval (0 = on-demand only) |
| `dwell_ms` | `120` | Per-channel capture window (clamped to 500) |

## Scope

Passive receive only — this module never transmits. It surveys the local
RF environment for situational awareness; it does not join, probe, or
interact with any network.
