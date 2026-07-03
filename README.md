# GallusOS

**A Modular Embedded Operating Environment** — by [Gallus Gadgets](https://github.com/stokemctoke/GallusOS).

GallusOS is a reusable embedded operating environment for ESP32-class
devices. It is built on **ESP-IDF 5.5.1** with modern C++ (no exceptions,
no RTTI) and provides a layered architecture: kernel, HAL, drivers,
services, compile-time modules, a versioned REST API, a WebSocket push
channel, and a browser dashboard with over-the-air firmware updates.

Primary target hardware: **Seeed Studio XIAO ESP32-C5** (8 MB flash).

---

## Features

| Area | What you get |
|---|---|
| **Kernel** | Event bus (bounded, drop-oldest), FreeRTOS scheduler with priority tiers, structured logging, `Result<T>` / `Status` error handling |
| **HAL** | Board pin facts, critical-pin protection, chip-specific constants |
| **Drivers** | GPIO, I2C, SSD1306 OLED, ADC with calibration |
| **Services** | LittleFS storage, JSON config, GPIO reservation, WiFi + captive provisioning, mDNS, SNTP, REST server, OLED display + splash, battery monitor, OTA with rollback, web dashboard |
| **Modules** | Compile-time plugins with `manifest.json`, CMake codegen, lifecycle manager |
| **Web** | Gzipped dashboard at `/`, live telemetry on `/ws`, OTA upload from the browser |
| **REST API** | Versioned under `/api/v1/` with optional bearer-token auth |

---

## Hardware

**Seeed Studio XIAO ESP32-C5**

- ESP32-C5: single-core RISC-V @ 240 MHz, 384 KB SRAM, 8 MB flash
- Dual-band Wi-Fi 6, BLE 5, 802.15.4
- Onboard user LED (GPIO27), BOOT button (GPIO28)
- Battery sense: GPIO6 (ADC, 1:2 divider) with enable on GPIO26
- I2C display bus: SDA GPIO23, SCL GPIO24 (SSD1306 128×64)

Reserved system pins (not available to modules): **6, 23, 24, 26, 27, 28**.

---

## Requirements

- **ESP-IDF 5.5.1** with the `esp32c5` target support
- Python 3 (bundled with ESP-IDF)
- USB connection to the XIAO (typically `/dev/ttyACM0` on Linux)
- Optional: Python **Pillow** in `tools/.venv/` if you regenerate splash
  bitmaps from PNG assets (see [Tools](#tools))

---

## Building from scratch

Clone the repository and build. No pre-built binaries or local `sdkconfig`
are required — defaults come from `sdkconfig.defaults`, and managed
components are downloaded on the first build.

```bash
git clone https://github.com/stokemctoke/GallusOS.git
cd GallusOS

# Activate ESP-IDF (adjust path to your installation)
. /path/to/esp-idf/export.sh

# One-time target selection (creates sdkconfig from sdkconfig.defaults)
idf.py set-target esp32c5

# Build (fetches littlefs + mdns via the Component Manager)
idf.py build
```

Flash and monitor:

```bash
idf.py -p /dev/ttyACM0 flash monitor
```

Clean rebuild:

```bash
idf.py fullclean
idf.py build
```

---

## First boot and WiFi

On first boot (or when STA credentials are missing), GallusOS starts a
SoftAP provisioning portal:

1. Connect to WiFi SSID **`GallusOS-XXXX`** (last four hex digits of the
   STA MAC address — also shown on the OLED and in the serial log).
2. Open the captive portal in your browser and enter your home WiFi SSID
   and password.
3. The device reboots into STA mode, obtains a DHCP address, and announces
   itself via mDNS as **`http://gallus-XXXX.local`**.

Credentials are stored in LittleFS config and persist across reboots.

---

## Web dashboard

Once connected to your network, open the device hostname in a browser:

```
http://gallus-XXXX.local/
```

The dashboard shows system status, live telemetry (WebSocket), module
list, OTA upload, and developer tabs for **Diagnostics**, **Filesystem**
browsing, and a **REST explorer**.

Live events arrive on **`ws://gallus-XXXX.local/ws`** (battery, WiFi,
module lifecycle, OTA progress).

The dashboard HTML lives in [`web/dashboard/index.html`](web/dashboard/index.html).
It is gzipped at configure time and embedded into the firmware, so it
always matches the running version.

---

## REST API (v1)

All JSON endpoints are under `/api/v1/`. When the config key
`system/api_token` is set, requests must include:

```
Authorization: Bearer <token>
```

An empty token (the factory default) leaves the API open — a warning is
logged at startup.

| Method | Path | Description |
|---|---|---|
| `GET` | `/api/v1/system` | Name, version, IDF version, uptime, heap, boot count |
| `GET` | `/api/v1/gpio` | Pin reservation snapshot |
| `GET` | `/api/v1/modules` | Registered modules, versions, categories, state |
| `GET` | `/api/v1/battery` | Millivolts and percentage |
| `GET` | `/api/v1/diagnostics` | Heap, tasks, event-bus and filesystem stats |
| `GET` | `/api/v1/files/list?path=/fs` | List LittleFS directory entries |
| `GET` | `/api/v1/files/read?path=/fs/...` | Read a text file (up to 8 KB) |
| `GET` | `/api/v1/i2c/scan` | Scan the I2C bus for devices |
| `GET` | `/api/v1/endpoints` | Catalogue of built-in API routes |
| `POST` | `/api/v1/ota/upload` | Raw firmware binary (same file as `build/gallus_os.bin`) |

Example:

```bash
curl http://gallus-0f30.local/api/v1/system
curl http://gallus-0f30.local/api/v1/battery
curl -X POST --data-binary @build/gallus_os.bin \
  http://gallus-0f30.local/api/v1/ota/upload
```

Modules may register additional routes under
`/api/v1/modules/<name>/…` via the SDK.

---

## OTA and rollback

Flash layout uses **dual OTA app slots** (3 MB each) plus a 1.9 MB
LittleFS partition — see [`partitions.csv`](partitions.csv).

1. Upload a new image via the dashboard or `POST /api/v1/ota/upload`.
2. The inactive OTA slot is written, validated, and set as the boot
   partition; the device reboots.
3. On first boot of the new image, the bootloader marks it **pending
   verify**.
4. After all services and modules start successfully, GallusOS calls
   `esp_ota_mark_app_valid_cancel_rollback()`.
5. If the new image crash-loops before that, the bootloader **rolls back**
   to the previous firmware automatically.

---

## Project layout

```
GallusOS/
├── main/                       Application entry point and built-in API routes
├── components/
│   ├── gallus_kernel/          Event bus, scheduler, logging, errors
│   ├── gallus_hal/             Board and chip facts
│   ├── gallus_drivers/         GPIO, I2C, SSD1306, ADC
│   ├── gallus_services/        Storage, config, WiFi, REST, OTA, display, …
│   └── gallus_sdk/             Module base class, registry, manager
├── modules/                    Compile-time application modules
│   ├── hello_world/            Minimal SDK example
│   ├── gpio_blink/             GPIO reservation + blink demo
│   ├── system_info/            Periodic diagnostics logging
│   └── i2c_scanner/            I2C bus scan demo
├── web/dashboard/              Browser dashboard (embedded at build time)
├── tools/                      Manifest codegen, SDK CLI, image tool
├── tests/host/                 Host-side manifest validation tests
├── assets/splash/              Source PNGs for boot splash (optional regen)
├── partitions.csv              Flash partition table
├── sdkconfig.defaults          Default Kconfig (committed; sdkconfig is not)
├── LICENSE                     PolyForm Perimeter License 1.0.0
└── NOTICE                      Plain-language license summary
```

Directories `docs/` and `examples/` are reserved for future documentation
and sample projects.

---

## Writing a module

Each module is a directory under `modules/` containing at minimum:

| File | Purpose |
|---|---|
| `manifest.json` | Name, version, category, required services/GPIO |
| `module.cpp` | Subclass of `gallus::sdk::Module` |
| `CMakeLists.txt` | Uses the `gallus_module()` macro |
| `config.json` | Default config namespace (optional) |

At build time, `tools/gallus_manifest_gen.py` validates the manifest and
generates a static registration stub. Modules link with `WHOLE_ARCHIVE` so
the registrar survives linking.

Example modules:

- **`hello_world`** — logging and scheduler only
- **`gpio_blink`** — requests GPIO27 via `GpioService`, toggles the onboard LED
- **`system_info`** — logs heap, event-bus and scheduler stats on a schedule
- **`i2c_scanner`** — scans the shared I2C bus and logs found addresses

Disable a module at runtime via config: namespace `modules`, key `<name>` =
`false`.

---

## Tools

| Script | Purpose |
|---|---|
| `tools/gallus.py` | SDK CLI: `create-module`, `validate-module`, `validate-all`, `build`, `flash`, `monitor` |
| `tools/gallus_manifest_gen.py` | Validates `manifest.json`, emits C++ registration |
| `tools/gallus_module.cmake` | CMake macro called from module `CMakeLists.txt` |
| `tools/gallus_image_gen.py` | Converts 128×64 PNG splash art to SSD1306 C arrays |

Run host tests (no hardware required):

```bash
python3 tests/host/run_tests.py
```

To regenerate splash bitmaps (optional — pre-generated header is committed):

```bash
python3 -m venv tools/.venv
tools/.venv/bin/pip install Pillow
tools/.venv/bin/python tools/gallus_image_gen.py \
  assets/splash/gallus-os_gg-logo_black-on-white.png \
  --name Logo \
  -o components/gallus_services/include/gallus/services/splash_frames.hpp
# … repeat for other frames, or extend the script for batch input
```

---

## Partition table

| Partition | Type | Size | Role |
|---|---|---|---|
| `nvs` | data | 24 KB | WiFi / system NVS |
| `otadata` | data | 8 KB | OTA boot selection |
| `phy_init` | data | 4 KB | RF calibration |
| `ota_0` | app | 3 MB | Firmware slot A |
| `ota_1` | app | 3 MB | Firmware slot B |
| `littlefs` | data | 1.9 MB | Config and file storage |

---

## Development status

| Phase | Scope | Status |
|---|---|---|
| 0 | Project skeleton, kernel | Done |
| 1 | HAL, drivers, storage, config, GPIO | Done |
| 2 | REST, WiFi, provisioning, modules | Done |
| 3 | mDNS, SNTP, module manager, API routes | Done |
| 4 | Module codegen, example modules | Done |
| 5 | Display, battery, OTA, web dashboard | Done |
| 6 | SDK CLI, diagnostics UI, REST explorer, filesystem browser, host tests, CI | In progress |

| Version | Value |
|---|---|
| Firmware | 0.1.0 |
| REST API | v1 |
| Module API | not yet frozen |

Phase 6 work is on branch **`stage-6`**.

### Phase 6 additions

- **`tools/gallus.py`** — SDK CLI (`create-module`, `validate-module`, `validate-all`, `build`, `flash`, `monitor`)
- **REST:** `/api/v1/diagnostics`, `/api/v1/files/list`, `/api/v1/files/read`, `/api/v1/i2c/scan`, `/api/v1/endpoints`
- **Dashboard tabs:** Diagnostics, Filesystem browser, REST explorer
- **Modules:** `system_info`, `i2c_scanner`
- **CI:** `.github/workflows/ci.yml` + host manifest tests in `tests/host/`

---

## License

GallusOS is **source-available** under the
[PolyForm Perimeter License 1.0.0](LICENSE).

In plain terms:

- Use, modify, and build on it freely — including commercially.
- Sell devices, gadgets, applications, and modules that run on GallusOS.
- **Give credit:** keep the attribution notice when you use or fork it.
- You may **not** repackage GallusOS and sell it *as* an OS, firmware
  platform, or SDK.

See [NOTICE](NOTICE) for a plain-language summary and third-party
component licenses (ESP-IDF, littlefs, mdns).

---

## Attribution

When you distribute GallusOS or a derivative, include:

> Required Notice: Copyright 2026 Gallus Gadgets. GallusOS
> (https://github.com/stokemctoke/GallusOS)
