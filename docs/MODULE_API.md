# GallusOS Module API v1

**Status:** Frozen as of GallusOS **v0.1.0** (July 2026).

Module authors can rely on the contracts below. Breaking changes require a
semver bump of the **Module API** (documented in the root README) and a
migration note in `CHANGELOG.md`.

---

## Overview

Modules are **compile-time plugins** under `modules/<name>/`. Each module:

1. Ships a `manifest.json` validated at build time.
2. Implements a subclass of `gallus::sdk::Module`.
3. Registers via the `GALLUS_MODULE(Class, name)` macro (codegen links the factory).
4. Is started/stopped by `ModuleManager` — no `loop()`, no runtime loading.

Modules are **event- and scheduler-driven**. They subscribe to events, schedule
jobs, and use services for all hardware access.

---

## Lifecycle

| Phase | Method | Purpose |
|---|---|---|
| Bind | `initialize(ModuleContext&)` | Store context; one-time setup |
| Start | `start()` | Subscribe, schedule jobs, request pins |
| Stop | `stop()` | Cancel jobs, unsubscribe, release pins |
| Shutdown | `shutdown()` | Final cleanup before restart |
| Routes | `registerRoutes(RestService&)` | Optional REST under `/api/v1/modules/<name>/` |

States tracked by `ModuleManager`: Registered → Initialized → Started → Stopped
(or Disabled / Failed).

Enable/disable via config namespace `modules`, key `<module_name>` (bool, default `true`).
At runtime, use `POST /api/v1/modules/<name>/enable|disable|start|stop` — enable persists
and initializes without starting; disable stops, tears down, and persists.

---

## ModuleContext (v1)

The only services exposed to modules in v1:

| Field | Service | Typical use |
|---|---|---|
| `events` | EventBus | Subscribe/publish framework events |
| `scheduler` | Scheduler | Periodic and delayed jobs |
| `config` | ConfigService | Read/write JSON namespaces in LittleFS |
| `storage` | StorageService | Raw file access under `/fs` |
| `gpio` | GpioService | Pin reservation, configure, read/write |
| `rest` | RestService | Register module HTTP routes |
| `i2c` | I2cService | Bus scan and device access |
| `wifi` | WifiService | Dual-band scan (ESP32-C5) |

**Not** on ModuleContext (use REST/events or future API revisions): display,
battery, OTA, network, time.

---

## Manifest schema (v1)

Required fields: `name`, `version`, `description`, `author`, `license`, `category`.

Optional: `menu_icon`, `required_services`, `required_gpio`, `events_published`,
`events_consumed`, `capabilities`, `config_schema`.

- `name` — lower_snake_case, must match directory name.
- `version` — semver `x.y.z` (independent of firmware version).
- `category` — one of the values validated by `tools/gallus_manifest_gen.py`.
- `required_services` — metadata for tooling/docs; not enforced at runtime in v1.

Validate with: `python3 tools/gallus.py validate-module modules/<name>`.

---

## Config convention

Runtime config lives at `/fs/config/<namespace>.json`. Keys are read with
`config.getInt(namespace, key, default)` etc.

Ship a `config.json` example in the module directory for documentation. Flat or
nested JSON is allowed; be consistent within a module.

---

## Events (framework)

Common events modules may publish or consume (see `gallus/event_bus.hpp`):

- `SystemReady`, `ModuleStarted`, `ModuleStopped`
- `WiFiConnected`, `WiFiDisconnected`
- `GPIOChanged` (module-defined payloads allowed for custom events in future)

---

## Versioning rules

| Artifact | Version | Frozen at v0.1.0 |
|---|---|---|
| Firmware | `0.1.0` in `gallus/version.hpp` | Yes |
| REST API | `/api/v1/` | Yes — additive routes only |
| Module API | v1 (this document) | Yes |
| Module manifests | per-module `0.1.0` | Independent semver |

**Allowed without Module API bump:** new optional manifest fields, new modules,
new REST routes, new services **if** ModuleContext is extended in a backward-
compatible way (new fields only).

**Requires Module API v2:** removing/renaming ModuleContext fields, changing
lifecycle order, breaking manifest required fields.

---

## Tooling

```bash
python3 tools/gallus.py validate-all    # manifest schema
python3 tools/gallus.py lint            # structure + context alignment
python3 tools/gallus.py generate-docs   # regenerate docs/MODULES.md
python3 tools/gallus.py host-sim        # desktop smoke test
```

---

## Reference module

See `modules/hello_world/` for the smallest valid module and `modules/wifi_scan/`
for a service-backed module using `WifiService::scan()`.
