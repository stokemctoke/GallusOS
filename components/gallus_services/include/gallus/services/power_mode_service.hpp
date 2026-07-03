#pragma once

#include <cstdint>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "gallus/error.hpp"
#include "gallus/event_bus.hpp"
#include "gallus/services/battery_service.hpp"
#include "gallus/services/display_service.hpp"
#include "gallus/services/wifi_service.hpp"

/// @file power_mode_service.hpp
/// @brief Low-power charge mode orchestration.
///
/// Entering charge mode stops application modules and WiFi, slows battery
/// sampling to a charge-friendly interval, and switches the OLED to a
/// minimal charge display. Exit restores normal operation.
///
/// Entry: Boot button long-press (~2 s) or REST / web UI.
/// Exit: Boot button short-press or REST / web UI.

namespace gallus::services {

class PowerModeService {
public:
    /// Payload carried by EventId::PowerModeChanged.
    struct ChangedPayload {
        uint8_t charge_mode;  // 0 = normal, 1 = charge
    };

    using ModuleFn = Status (*)(void* ctx);

    struct ModuleHooks {
        ModuleFn stop_all = nullptr;
        ModuleFn start_all = nullptr;
        void* ctx = nullptr;
    };

    PowerModeService(EventBus& events, WifiService& wifi,
                     BatteryService& battery, DisplayService& display)
        : events_(events),
          wifi_(wifi),
          battery_(battery),
          display_(display) {}
    PowerModeService(const PowerModeService&) = delete;
    PowerModeService& operator=(const PowerModeService&) = delete;

    void setModuleHooks(ModuleHooks hooks) { module_hooks_ = hooks; }

    /// Configure the boot button and start the monitor task.
    Status init();

    /// Start the boot-button monitor (after WiFi and modules are up).
    Status start();

    Status enterChargeMode();
    Status exitChargeMode();

    [[nodiscard]] bool chargeMode() const { return charge_mode_; }

private:
    static void buttonTaskEntry(void* arg);
    void buttonLoop();
    void publishMode(bool charge);

    EventBus& events_;
    WifiService& wifi_;
    BatteryService& battery_;
    DisplayService& display_;
    ModuleHooks module_hooks_ = {};
    TaskHandle_t button_task_ = nullptr;
    bool charge_mode_ = false;
    bool initialized_ = false;
};

}  // namespace gallus::services
