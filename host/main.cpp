/// @file main.cpp
/// @brief Host simulation: kernel, mock services, and hello_world module.

#include <chrono>
#include <cstdio>
#include <cstring>
#include <thread>

#include "gallus/kernel.hpp"
#include "gallus/sdk/module_manager.hpp"
#include "gallus/services/config_service.hpp"
#include "gallus/services/gpio_service.hpp"
#include "gallus/services/i2c_service.hpp"
#include "gallus/services/rest_service.hpp"
#include "gallus/services/storage_service.hpp"

namespace {

constexpr const char* kTag = "HostSim";

volatile bool g_ready = false;
volatile bool g_module_started = false;

void onSystemReady(const gallus::Event& event, void* /*ctx*/) {
    if (event.id == gallus::EventId::SystemReady) {
        g_ready = true;
    }
}

void onModuleStarted(const gallus::Event& event, void* /*ctx*/) {
    const auto* payload = event.as<gallus::sdk::ModuleManager::ModuleEventPayload>();
    if (payload == nullptr) {
        return;
    }
    if (std::strcmp(payload->name, "hello_world") == 0) {
        g_module_started = true;
    }
}

bool seedHelloWorldConfig(gallus::services::StorageService& storage) {
    static constexpr const char* kJson =
        R"({"message":"Hello from GallusOS host!","period_s":1})";
    return storage
        .writeFile("/fs/config/hello_world.json", kJson, std::strlen(kJson))
        .ok();
}

const gallus::sdk::ModuleManager::Entry* findModule(
    const gallus::sdk::ModuleManager& modules, const char* name) {
    for (size_t i = 0; i < modules.count(); ++i) {
        if (std::strcmp(modules.at(i).info->name, name) == 0) {
            return &modules.at(i);
        }
    }
    return nullptr;
}

}  // namespace

int main() {
    gallus::Kernel& kernel = gallus::Kernel::instance();

    static gallus::services::StorageService storage;
    static gallus::services::ConfigService config(storage, kernel.events());
    static gallus::services::GpioService gpio(kernel.events());
    static gallus::services::RestService rest(config);
    static gallus::services::I2cService i2c;

    if (!kernel.init().ok()) {
        std::fprintf(stderr, "host sim: kernel init failed\n");
        return 1;
    }

    if (!storage.init().ok() || !config.init().ok() || !gpio.init().ok() ||
        !rest.init().ok() || !i2c.init(23, 24).ok()) {
        std::fprintf(stderr, "host sim: service init failed\n");
        return 1;
    }

    if (!seedHelloWorldConfig(storage)) {
        std::fprintf(stderr, "host sim: failed to seed hello_world config\n");
        return 1;
    }

    (void)kernel.events().subscribe(gallus::EventId::SystemReady, &onSystemReady);
    (void)kernel.events().subscribe(gallus::EventId::ModuleStarted,
                                    &onModuleStarted);

    static gallus::sdk::ModuleContext module_ctx = {
        kernel.events(), kernel.scheduler(), config, storage,
        gpio,            rest,               i2c,
    };
    static gallus::sdk::ModuleManager modules(module_ctx);

    if (!modules.initAll().ok() || !kernel.start().ok() ||
        !modules.startAll().ok()) {
        std::fprintf(stderr, "host sim: module bring-up failed\n");
        return 1;
    }

    const auto deadline =
        std::chrono::steady_clock::now() + std::chrono::milliseconds(1800);
    while (std::chrono::steady_clock::now() < deadline) {
        if (g_ready && g_module_started) {
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    (void)modules.stopAll();

    const auto* hello = findModule(modules, "hello_world");
    const bool hello_started =
        hello != nullptr &&
        hello->state == gallus::sdk::ModuleManager::State::Stopped;
    const bool ok = g_ready && g_module_started && hello_started;

    std::printf(
        "host sim: ready=%d module=%d hello_state=%s delivered=%u jobs=%zu\n",
        g_ready ? 1 : 0, g_module_started ? 1 : 0,
        hello != nullptr ? gallus::sdk::ModuleManager::stateName(hello->state)
                         : "missing",
        kernel.events().deliveredCount(), kernel.scheduler().activeJobs());

    return ok ? 0 : 1;
}
