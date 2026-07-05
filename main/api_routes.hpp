#pragma once

#include "gallus/kernel.hpp"
#include "gallus/sdk/module_manager.hpp"
#include "gallus/services/battery_service.hpp"
#include "gallus/services/ble_service.hpp"
#include "gallus/services/config_service.hpp"
#include "gallus/services/diagnostics_service.hpp"
#include "gallus/services/power_mode_service.hpp"
#include "gallus/services/gpio_service.hpp"
#include "gallus/services/i2c_service.hpp"
#include "gallus/services/rest_service.hpp"
#include "gallus/services/wifi_service.hpp"
#include "gallus/services/storage_service.hpp"

/// @file api_routes.hpp
/// @brief Built-in API routes under /api/v1/.

namespace gallus::app {

struct ApiContext {
    services::RestService* rest;
    services::ConfigService* config;
    services::DiagnosticsService* diagnostics;
    services::GpioService* gpio;
    services::StorageService* storage;
    services::I2cService* i2c;
    sdk::ModuleManager* modules;
    services::BatteryService* battery;
    services::PowerModeService* power;
    services::WifiService* wifi;
    services::BleService* ble;
    Kernel* kernel;
};

/// Register the built-in routes. @p ctx must outlive the server.
Status registerApiRoutes(ApiContext& ctx);

}  // namespace gallus::app
