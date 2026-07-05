# ble_scan

On-demand Bluetooth Low Energy device scanner for the XIAO ESP32-C5.

The C5 supports **Bluetooth 5.0 LE only — there is no Bluetooth Classic
(BR/EDR)** on this chip. This module surveys nearby BLE *advertisements*
and reports, per device: address (public/random), RSSI, advertised name,
manufacturer (from the company ID in the advertisement), advertised
service UUIDs, whether it is connectable, and TX power when present.

## Heap model

The NimBLE host and BT controller are brought up **only for the duration
of a scan** and torn down afterwards (`BleService`), so BLE costs RAM
only while actively scanning — nothing at rest. BLE coexists with WiFi
via the coexistence arbiter, so a scan does not drop the dashboard's
WiFi connection.

## Usage

- **Dashboard:** Modules → `ble_scan` → Controls → **Scan now**. Click a
  device to expand its full detail.
- **REST:** `GET /api/v1/ble/scan?ms=3000`

## Config (`ble_scan` namespace)

| Key | Default | Meaning |
|---|---|---|
| `auto_start` | `false` | Start scanning in the background at boot |
| `period_ms` | `0` | Background scan interval (0 = on-demand only) |
| `duration_ms` | `3000` | Scan window per run (clamped to 10000) |
