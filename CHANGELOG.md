# Changelog

All notable changes to GallusOS are documented here. The project follows
[Semantic Versioning](https://semver.org/) for firmware and module manifests.

## [0.1.3] — 2026-07-05

Security follow-up to v0.1.2, driven by a field report that the API token
behaved inconsistently across browsers. Investigation ([issues #18–#25](https://github.com/stokemctoke/GallusOS/issues?q=is%3Aissue+is%3Aclosed))
found a root-cause auth bug and a remotely-triggerable XSS.

### Security

- **API token now enforced immediately** — `RestService` subscribes to
  `ConfigChanged` and reloads the token on any write. Previously a saved token
  was persisted to flash but not enforced until the next reboot, leaving the
  API open in the meantime. (#18)
- **Dashboard XSS fixed** — device-supplied strings (WiFi SSIDs, file names,
  module metadata, config values) were rendered into `innerHTML` unescaped. A
  nearby AP with an HTML/script SSID could steal the API token from
  `localStorage` or drive authenticated actions (including OTA) the moment an
  operator ran a WiFi scan. All such values are now HTML-escaped, and the file
  list uses data-attributes + a delegated handler instead of inline `onclick`. (#24)
- **Constant-time token comparison** — the token is compared with an
  XOR-accumulating fixed-length check instead of `strcmp`, closing a timing
  side-channel. Over-length credentials are rejected rather than truncate-matched. (#25)

### Dashboard token UX

- **Confirmation field** — the API token must be entered twice (and match a
  URL-safe charset, max 63 chars) before saving, so a typo can't lock out every
  client. (#23)
- **Explicit Clear-token button** — a blank re-save no longer silently disables
  auth; clearing the token is now a deliberate, confirmed action. (#19)
- **Reboot button** reports real failures instead of always showing
  "Rebooting…" (it now checks the response). (#20)
- **WebSocket reconnect** uses exponential backoff and stops once a token is
  required but not held, ending the device-log reject storm. (#21)
- **OTA upload** surfaces a 401 as a token prompt rather than a generic
  firmware error. (#22)

[0.1.3]: https://github.com/stokemctoke/GallusOS/releases/tag/v0.1.3

## [0.1.2] — 2026-07-04

Hardening release: every finding from the July 2026 full-codebase review fixed
([issues #1–#17](https://github.com/stokemctoke/GallusOS/issues?q=is%3Aissue+is%3Aclosed)),
one commit per issue.

### Security & auth

- **Dashboard token support** — all dashboard requests (REST, OTA upload, WebSocket)
  now send `Authorization: Bearer <token>`; a 401 raises an unlock banner and the
  token persists in the browser. Previously, setting a token locked the dashboard out.
- **`/ws` requires the token** — via header or `?token=` query; unauthorized sockets
  are closed. Device logs/telemetry no longer stream without auth.
- **`/api/v1/files/read` refuses `/fs/config`** — raw config files leaked the WiFi
  password and API token past the config API's redaction.

### Stability

- **Fixed guaranteed stack overflow** in `/api/v1/files/read` (8 KB buffer on the
  6 KB httpd stack) — buffer is now heap-allocated
- **Bounded task snapshot** — diagnostics use `uxTaskGetSystemState` instead of the
  unbounded `vTaskList`; dashboard shows per-task priority, stack high-water mark, state
- **OTA timeout cap** — a stalled upload aborts after ~50 s instead of wedging the
  HTTP server until power-cycle
- **Charge mode over HTTP** — response is sent before the radio stops, so the
  dashboard no longer reports an error on success

### Kernel & module SDK

- **Scheduler**: one-shot jobs no longer leak their slot/timer when a tier queue is
  full; `cancel()` now fences on in-flight execution (safe to free ctx after return);
  timer callbacks carry an immutable slot+generation and skip recycled slots
- **Module teardown**: new `unregisterRoutes()` lifecycle hook — routes can no longer
  outlive their module instance; route-registration failure fails the start instead
  of running half-wired
- **GPIO ownership**: `releasePin()` rejects callers that don't hold the pin;
  I2C pins 23/24 are Reserved at boot (forced claim only)
- **EventBus**: unsubscribe grace period documented (ctx lifetime contract)

### Provisioning

- **Long / special-character WiFi passwords survive setup** — portal buffers sized
  for URL-encoded credentials, truncation rejected with a clear error

### Dashboard & config

- **Live log fixed** — whole lines, ANSI colour codes stripped (frames were invalid
  JSON and silently dropped); one WebSocket frame per log line
- **Float config values** — `PUT /api/v1/config` stores JSON numbers as doubles
  instead of truncating to int32

### Tooling & CI

- Manifest generator enforces the 23-char module name limit at build time (new test)
- HTTP route budget raised to 40 with slot-usage reporting on failure
- CI: ESP-IDF pin restored via the install action's renamed `version` input
  (the old input names were silently ignored, installing IDF 6.x and breaking builds)

### Upgrade notes

- If you set an API token, use only letters, digits, `-`, `_`, `.` (max 63 chars) —
  it is compared raw in both the HTTP header and the WebSocket query string.
- Module authors: if you override `registerRoutes()`, you MUST override
  `unregisterRoutes()` (see `docs/MODULE_API.md`).

[0.1.2]: https://github.com/stokemctoke/GallusOS/releases/tag/v0.1.2

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
