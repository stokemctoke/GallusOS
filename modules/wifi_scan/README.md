# wifi_scan

Periodic dual-band WiFi survey for ESP32-C5 (2.4 GHz and 5 GHz). Logs
visible networks with RSSI, channel, and band.

## Configuration

Namespace `wifi_scan` in LittleFS config.

| Key | Default | Description |
|---|---|---|
| `period_ms` | `60000` | Scan interval in milliseconds |
| `band` | `both` | `2.4`, `5`, or `both` |
