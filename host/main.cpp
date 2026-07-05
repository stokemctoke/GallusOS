/// @file main.cpp
/// @brief Host simulation: kernel, mock services, and all example modules.

#include <chrono>
#include <cstdio>
#include <cstring>
#include <thread>

#include "gallus/kernel.hpp"
#include "gallus/sdk/module_manager.hpp"
#include "gallus/services/ble_service.hpp"
#include "gallus/services/config_service.hpp"
#include "gallus/services/gpio_service.hpp"
#include "gallus/services/i2c_service.hpp"
#include "gallus/services/rest_service.hpp"
#include "gallus/services/storage_service.hpp"
#include "gallus/services/wifi_service.hpp"

namespace {

constexpr const char* kTag = "HostSim";

volatile bool g_ready = false;
volatile unsigned g_modules_started = 0;

void onSystemReady(const gallus::Event& event, void* /*ctx*/) {
    if (event.id == gallus::EventId::SystemReady) {
        g_ready = true;
    }
}

void onModuleStarted(const gallus::Event& event, void* /*ctx*/) {
    if (event.id == gallus::EventId::ModuleStarted) {
        ++g_modules_started;
    }
}

bool writeConfig(gallus::services::StorageService& storage, const char* path,
                 const char* json) {
    return storage.writeFile(path, json, std::strlen(json)).ok();
}

bool seedConfigs(gallus::services::StorageService& storage) {
    return writeConfig(storage, "/fs/config/hello_world.json",
                       R"({"message":"Hello from GallusOS host!","period_s":1})") &&
           writeConfig(storage, "/fs/config/gpio_blink.json",
                       R"({"pin":27,"period_ms":400})") &&
           writeConfig(storage, "/fs/config/i2c_scanner.json",
                       R"({"auto_start":true,"period_ms":100})") &&
           writeConfig(storage, "/fs/config/system_info.json",
                       R"({"auto_start":true,"period_ms":100})") &&
           writeConfig(storage, "/fs/config/wifi_scan.json",
                       R"({"auto_start":true,"period_ms":100,"band":"both"})");
}

unsigned countActiveModules(const gallus::sdk::ModuleManager& modules) {
    unsigned count = 0;
    for (size_t i = 0; i < modules.count(); ++i) {
        const auto& entry = modules.at(i);
        if (entry.state == gallus::sdk::ModuleManager::State::Disabled) {
            continue;
        }
        if (!modules.autoStart(entry.info->name)) {
            continue;
        }
        ++count;
    }
    return count;
}

bool allModulesStopped(const gallus::sdk::ModuleManager& modules) {
    for (size_t i = 0; i < modules.count(); ++i) {
        const auto& entry = modules.at(i);
        if (entry.state == gallus::sdk::ModuleManager::State::Disabled ||
            entry.state == gallus::sdk::ModuleManager::State::Failed) {
            continue;
        }
        if (entry.state != gallus::sdk::ModuleManager::State::Stopped) {
            return false;
        }
    }
    return true;
}

}  // namespace

int main() {
    gallus::Kernel& kernel = gallus::Kernel::instance();

    static gallus::services::StorageService storage;
    static gallus::services::ConfigService config(storage, kernel.events());
    static gallus::services::GpioService gpio(kernel.events());
    static gallus::services::RestService rest(config, kernel.events());
    static gallus::services::I2cService i2c;
    static gallus::services::WifiService wifi(config, kernel.events(), rest);
    static gallus::services::BleService ble;

    if (!kernel.init().ok()) {
        std::fprintf(stderr, "host sim: kernel init failed\n");
        return 1;
    }

    if (!storage.init().ok() || !config.init().ok() || !gpio.init().ok() ||
        !rest.init().ok() || !i2c.init(23, 24).ok() || !wifi.init().ok()) {
        std::fprintf(stderr, "host sim: service init failed\n");
        return 1;
    }

    if (!seedConfigs(storage)) {
        std::fprintf(stderr, "host sim: failed to seed module configs\n");
        return 1;
    }

    (void)kernel.events().subscribe(gallus::EventId::SystemReady, &onSystemReady);
    (void)kernel.events().subscribe(gallus::EventId::ModuleStarted,
                                    &onModuleStarted);

    static gallus::sdk::ModuleContext module_ctx = {
        kernel.events(), kernel.scheduler(), config, storage,
        gpio,            rest,               i2c,    wifi,
        ble,
    };
    static gallus::sdk::ModuleManager modules(module_ctx);

    if (!modules.initAll().ok() || !kernel.start().ok() ||
        !modules.startAll().ok()) {
        std::fprintf(stderr, "host sim: module bring-up failed\n");
        return 1;
    }

    const unsigned expected = countActiveModules(modules);
    const auto deadline =
        std::chrono::steady_clock::now() + std::chrono::milliseconds(2500);
    while (std::chrono::steady_clock::now() < deadline) {
        if (g_ready && g_modules_started >= expected) {
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    (void)modules.stopAll();

    const bool ok =
        g_ready && g_modules_started >= expected && allModulesStopped(modules);

    std::printf(
        "host sim: ready=%d modules=%u/%u stopped=%d delivered=%u jobs=%zu\n",
        g_ready ? 1 : 0, g_modules_started, expected, allModulesStopped(modules) ? 1 : 0,
        kernel.events().deliveredCount(), kernel.scheduler().activeJobs());

    return ok ? 0 : 1;
}
