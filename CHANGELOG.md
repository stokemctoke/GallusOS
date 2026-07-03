# Changelog

All notable changes to GallusOS are documented here. The project follows
[Semantic Versioning](https://semver.org/) for firmware and module manifests.

## [0.1.1] — 2026-07-03

Patch release: module lifecycle, on-demand diagnostics, and dashboard polish since v0.1.0.

### Dashboard

- **Modules tab** — dropdown module picker, Controls / Settings split, start/stop/enable/disable
- Module-specific controls: WiFi scan table, I2C address list, system info snapshot
- **REST explorer** — GET / POST / PUT method selector and optional JSON body

### Modules & API

- **Module runtime control** — `POST /api/v1/modules/<name>/{start|stop|enable|disable}`
- **On-demand defaults** — survey modules (`wifi_scan`, `i2c_scanner`, `system_info`) use `auto_start: false` and `period_ms: 0`
- **`auto_start` config key** — per-module boot behaviour without disabling entirely
- **Module POST route fix** — single trailing-wildcard handler `/api/v1/modules/*` (ESP-IDF requirement)

### Fixes

- **`wifi_scan` boot loop** — heap-allocated scan results, no synchronous scan on main task
- **WiFi scan while connected** — single scan pass with channel band filter (no `esp_wifi_set_band()` while STA linked)

### Flashing

Same as v0.1.0 — OTA upload of `build/gallus_os.bin` or USB flash. After upgrade, set survey module config if old LittleFS values still auto-scan:

```json
{ "auto_start": false, "period_ms": 0 }
```

[0.1.1]: https://github.com/stokemctoke/GallusOS/releases/tag/v0.1.1

## [0.1.0] — 2026-07-03

First public release of GallusOS on **Seeed XIAO ESP32-C5** (ESP-IDF 5.5.1).

### Kernel & platform

- Event bus with bounded queue and drop-oldest policy
- FreeRTOS scheduler with priority tiers and `Result<T>` / `Status` errors
- HAL for XIAO ESP32-C5 pin facts and critical-pin protection
- GPIO, I2C, SSD1306 OLED, and ADC battery drivers

### Services

- LittleFS storage and JSON config namespaces
- GPIO reservation manager
- WiFi STA + captive SoftAP provisioning portal
- mDNS hostname, SNTP time sync
- REST server (API v1) and WebSocket telemetry
- OLED display with splash screen and live status
- Battery monitor, OTA with rollback, web dashboard
- Diagnostics, power/charge mode, I2C bus service

### Modules (compile-time plugins)

- `hello_world`, `gpio_blink`, `i2c_scanner`, `system_info`
- `wifi_scan` — dual-band (2.4 / 5 GHz) WiFi survey module
- Manifest codegen via `tools/gallus_manifest_gen.py` and `gallus_module()` CMake macro

### Web & REST

- Gzipped dashboard at `/` with Diagnostics, Filesystem, REST explorer, Settings, Config editor
- Live serial log stream, charge mode, WiFi re-provision
- Config read/write API with secret redaction

### Developer experience

- `tools/gallus.py` SDK CLI: scaffold, validate, lint, generate-docs, host-sim, build/flash/monitor
- Desktop host simulation (`host/`) with pthread shims and mock services
- CI: manifest tests + host sim + firmware build
- Module API v1 documented in `docs/MODULE_API.md`

### Flashing

```bash
git clone https://github.com/stokemctoke/GallusOS.git
cd GallusOS
. /path/to/esp-idf/export.sh
idf.py set-target esp32c5
idf.py -p /dev/ttyACM0 flash monitor
```

First boot without WiFi credentials starts provisioning SoftAP **GallusOS-XXXX** at `192.168.42.1`.

[0.1.0]: https://github.com/stokemctoke/GallusOS/releases/tag/v0.1.0
