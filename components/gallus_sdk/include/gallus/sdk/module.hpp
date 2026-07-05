#pragma once

#include <cstddef>

#include "gallus/error.hpp"
#include "gallus/event_bus.hpp"
#include "gallus/scheduler.hpp"
#include "gallus/services/config_service.hpp"
#include "gallus/services/gpio_service.hpp"
#include "gallus/services/i2c_service.hpp"
#include "gallus/services/ble_service.hpp"
#include "gallus/services/rest_service.hpp"
#include "gallus/services/storage_service.hpp"
#include "gallus/services/wifi_service.hpp"

/// @file module.hpp
/// @brief The GallusOS Module SDK: what a module is and what it gets.
///
/// Modules are compile-time plugins living in /modules/<name>/. Each
/// ships a manifest.json which is validated and turned into
/// registration code at BUILD time (tools/gallus_manifest_gen.py via
/// the gallus_module() CMake macro) — nothing is parsed at runtime
/// and no framework file is edited to add a module.
///
/// Modules are event- and scheduler-driven: there is deliberately no
/// loop(). Subscribe to events, register periodic jobs, and a module
/// that has nothing to do costs nothing.

namespace gallus::sdk {

/// Everything a module may touch. Modules never access drivers or
/// hardware directly — services enforce the policy.
struct ModuleContext {
    EventBus& events;
    Scheduler& scheduler;
    services::ConfigService& config;
    services::StorageService& storage;
    services::GpioService& gpio;
    services::RestService& rest;
    services::I2cService& i2c;
    services::WifiService& wifi;
    services::BleService& ble;
};

/// Module metadata, generated from manifest.json at build time.
struct ModuleInfo {
    const char* name;
    const char* version;
    const char* description;
    const char* author;
    const char* license;
    const char* category;
    const char* menu_icon;
    const char* const* required_services;
    size_t required_service_count;
    const int* required_gpio;
    size_t required_gpio_count;
    const char* const* events_published;
    size_t events_published_count;
    const char* const* events_consumed;
    size_t events_consumed_count;
};

/// Base class for all modules. The framework (ModuleManager) owns the
/// lifecycle: initialize -> start -> [stop -> start ...] -> shutdown.
class Module {
public:
    virtual ~Module() = default;

    /// Bind the context. Override for one-time setup; call the base
    /// (or store the context yourself) before using ctx().
    virtual Status initialize(ModuleContext& ctx) {
        ctx_ = &ctx;
        return Status::success();
    }

    /// Begin doing work: subscribe to events, schedule jobs, request
    /// pins. Must be reversible by stop().
    virtual Status start() = 0;

    /// Undo start(): cancel jobs, unsubscribe, release pins.
    virtual Status stop() { return Status::success(); }

    /// Final cleanup before destruction (system shutdown/restart).
    virtual void shutdown() {}

    /// Register module REST routes (under /api/v1/modules/<name>/).
    virtual Status registerRoutes(services::RestService& rest) {
        (void)rest;
        return Status::success();
    }

    /// Undo registerRoutes(): remove every route this module added.
    /// A module that overrides registerRoutes() MUST override this
    /// too — the framework calls it on stop and before the instance
    /// is destroyed, so no route may outlive the module.
    virtual void unregisterRoutes(services::RestService& rest) {
        (void)rest;
    }

protected:
    /// Valid after initialize().
    [[nodiscard]] ModuleContext& ctx() { return *ctx_; }

private:
    ModuleContext* ctx_ = nullptr;
};

using ModuleFactory = Module* (*)();

}  // namespace gallus::sdk

/// Defines the factory the generated registration code links against.
/// Place at file scope in the module's .cpp:
///   GALLUS_MODULE(HelloWorldModule, hello_world)
#define GALLUS_MODULE(ClassName, module_name)          \
    gallus::sdk::Module* gallus_create_##module_name() { \
        return new ClassName();                          \
    }
