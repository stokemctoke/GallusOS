# ieee802154_scan

On-demand IEEE 802.15.4 survey for the XIAO ESP32-C5 — the radio under
**Zigbee, Thread and Matter-over-Thread**, an IoT ecosystem invisible to
WiFi and BLE scans.

The survey sweeps all 16 channels (11–26). For each it measures channel
**energy** and does a brief **promiscuous capture** to enumerate the
**networks (PAN IDs)** and **device addresses** heard, plus which MAC
frame types are present (beacon / data / ack / command). Payloads are
network-key encrypted, so this reports **presence and metadata, not
decoded traffic** — the same information a Zigbee/Thread sniffer surfaces
before you have the network key.

## Heap & radio model

The 802.15.4 radio is brought up only for the survey and torn down after
(`Ieee802154Service`), so it costs RAM only while scanning. It shares the
2.4 GHz radio with WiFi/BLE via the coexistence arbiter and is never up
at the same time as the BLE stack (each is on-demand), so the dashboard
connection is preserved.

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
