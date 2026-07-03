#pragma once

#include "gallus/sdk/module_manager.hpp"
#include "gallus/services/battery_service.hpp"
#include "gallus/services/config_service.hpp"
#include "gallus/services/gpio_service.hpp"
#include "gallus/services/rest_service.hpp"

/// @file api_routes.hpp
/// @brief Built-in API routes: /api/v1/system, /api/v1/gpio,
/// /api/v1/modules, /api/v1/battery.

namespace gallus::app {

struct ApiContext {
    services::RestService* rest;
    services::ConfigService* config;
    services::GpioService* gpio;
    sdk::ModuleManager* modules;
    services::BatteryService* battery;
};

/// Register the built-in routes. @p ctx must outlive the server.
Status registerApiRoutes(ApiContext& ctx);

}  // namespace gallus::app
