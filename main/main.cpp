/// @file main.cpp
/// @brief GallusOS entry point — the composition root.
///
/// Deliberately thin: boot the kernel, bring up core services, start
/// the modules, then hand control to the event-driven framework.

#include <cstdint>

#include "esp_system.h"

#include "api_routes.hpp"
#include "gallus/hal/board.hpp"
#include "gallus/kernel.hpp"
#include "gallus/sdk/module_manager.hpp"
#include "gallus/services/battery_service.hpp"
#include "gallus/services/config_service.hpp"
#include "gallus/services/diagnostics_service.hpp"
#include "gallus/services/display_service.hpp"
#include "gallus/services/gpio_service.hpp"
#include "gallus/services/i2c_service.hpp"
#include "gallus/services/network_service.hpp"
#include "gallus/services/ota_service.hpp"
#include "gallus/services/rest_service.hpp"
#include "gallus/services/storage_service.hpp"
#include "gallus/services/time_service.hpp"
#include "gallus/services/webui_service.hpp"
#include "gallus/services/wifi_service.hpp"

namespace {

constexpr const char* kTag = "GallusOS";

void banner() {
    gallus::Log::info(kTag, "----------------------------------------");
    gallus::Log::info(kTag, " GallusOS v%s", gallus::Kernel::version());
    gallus::Log::info(kTag, " A Modular Embedded Operating Environment");
    gallus::Log::info(kTag, " (c) Gallus Gadgets");
    gallus::Log::info(kTag, "----------------------------------------");
}

/// Log-and-continue for service init: a degraded service must not
/// stop the rest of the system from coming up.
void check(const char* what, gallus::Status status) {
    if (!status.ok()) {
        gallus::Log::error(kTag, "%s failed: %s", what, status.message());
    }
}

}  // namespace

extern "C" void app_main(void) {
    banner();

    gallus::Kernel& kernel = gallus::Kernel::instance();

    gallus::Status status = kernel.init();
    if (!status.ok()) {
        gallus::Log::error(kTag, "kernel init failed: %s — restarting",
                           status.message());
        esp_restart();
    }

    // Core services. Function-local statics: they outlive app_main and
    // this is the one place where the object graph is assembled.
    static gallus::services::StorageService storage;
    static gallus::services::ConfigService config(storage, kernel.events());
    static gallus::services::GpioService gpio(kernel.events());
    static gallus::services::I2cService i2c;
    static gallus::services::RestService rest(config);
    static gallus::services::WifiService wifi(config, kernel.events(), rest);
    static gallus::services::NetworkService network(config, kernel.events());
    static gallus::services::TimeService time_service(kernel.events());
    static gallus::services::DisplayService display(
        kernel.events(), i2c);
    static gallus::services::BatteryService battery(
        kernel.events(), kernel.scheduler(),
        gallus::hal::board::kPinBatteryAdc,
        gallus::hal::board::kPinBatteryMeasureEnable);
    static gallus::services::OtaService ota(kernel.events(), rest);
    static gallus::services::WebUiService webui(kernel.events(), rest);
    static gallus::services::DiagnosticsService diagnostics(kernel, storage);

    check("storage init", storage.init());
    check("config init", config.init());
    check("gpio init", gpio.init());
    check("i2c init",
          i2c.init(gallus::hal::board::kPinI2cSda,
                   gallus::hal::board::kPinI2cScl));

    // Bring the OLED up early and play the splash while the rest of
    // the system (and WiFi) comes online. init() returns NotFound
    // when no panel is attached, which is fine — the board runs
    // headless.
    (void)display.init();
    display.playSplash();

    // Persistent boot counter (also exposed via /api/v1/system).
    const int32_t boot_count = config.getInt("system", "boot_count", 0) + 1;
    (void)config.setInt("system", "boot_count", boot_count);
    gallus::Log::info(kTag, "boot #%d", static_cast<int>(boot_count));

    // Networking stack: WiFi driver first, then the HTTP server, then
    // the services that piggyback on the connection events.
    check("wifi init", wifi.init());
    check("rest init", rest.init());
    check("network init", network.init());
    check("time init", time_service.init());

    // Modules: everything the manifest codegen registered at build time.
    static gallus::sdk::ModuleContext module_ctx = {
        kernel.events(), kernel.scheduler(), config, storage, gpio, rest, i2c,
    };
    static gallus::sdk::ModuleManager modules(module_ctx);
    check("modules init", modules.initAll());

    static gallus::app::ApiContext api_ctx = {&rest,    &config, &diagnostics,
                                              &gpio,    &storage, &i2c,
                                              &modules, &battery, &kernel};
    check("api routes", gallus::app::registerApiRoutes(api_ctx));

    check("ota init", ota.init());
    check("webui init", webui.init());
    check("battery init", battery.init());

    check("wifi start", wifi.start());
    check("modules start", modules.startAll());

    // Switch the OLED from splash to the live status screen, then
    // start battery sampling (its first reading updates the screen).
    check("display start", display.start());
    check("battery start", battery.start());

    check("kernel start", kernel.start());

    // Everything came up: keep this image (cancels OTA rollback).
    ota.confirmHealthy();

    // app_main returns; the kernel's tasks own the system from here.
}
