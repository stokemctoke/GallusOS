#include "gallus/services/power_mode_service.hpp"

#include "esp_timer.h"

#include "freertos/task.h"

#include "gallus/drivers/gpio.hpp"
#include "gallus/hal/board.hpp"
#include "gallus/log.hpp"

namespace gallus::services {

namespace {

constexpr const char* kTag = "Power";
constexpr uint32_t kButtonStackBytes = 3072;
constexpr UBaseType_t kButtonTaskPriority = 3;
constexpr int kBootPin = gallus::hal::board::kPinBootButton;

constexpr uint32_t kPollMs = 50;
constexpr uint32_t kLongPressMs = 2000;
constexpr uint32_t kShortPressMaxMs = 600;

constexpr uint32_t kNormalSampleMs = 30000;
constexpr uint32_t kChargeSampleMs = 5000;

int64_t nowMs() {
    return esp_timer_get_time() / 1000;
}

}  // namespace

Status PowerModeService::init() {
    if (initialized_) {
        return Error::InvalidState;
    }

    GALLUS_RETURN_IF_ERROR(
        drivers::GpioDriver::configure(kBootPin, drivers::PinMode::InputPullUp));

    initialized_ = true;
    return Status::success();
}

Status PowerModeService::start() {
    if (!initialized_) {
        return Error::InvalidState;
    }
    if (button_task_ != nullptr) {
        return Status::success();
    }

    const BaseType_t ok =
        xTaskCreate(&PowerModeService::buttonTaskEntry, "gallus_btn",
                    kButtonStackBytes, this, kButtonTaskPriority, &button_task_);
    if (ok != pdPASS) {
        button_task_ = nullptr;
        return Error::NoMemory;
    }
    Log::info(kTag, "boot button ready (hold 2s = charge mode)");
    return Status::success();
}

void PowerModeService::buttonTaskEntry(void* arg) {
    static_cast<PowerModeService*>(arg)->buttonLoop();
}

void PowerModeService::buttonLoop() {
    bool was_pressed = false;
    int64_t press_start = 0;
    bool long_fired = false;

    while (true) {
        const auto level = drivers::GpioDriver::read(kBootPin);
        const bool pressed = level.ok() && !level.value();
        const int64_t now = nowMs();

        if (pressed && !was_pressed) {
            press_start = now;
            long_fired = false;
        }

        if (pressed && !long_fired && (now - press_start) >= kLongPressMs) {
            long_fired = true;
            if (!charge_mode_) {
                (void)enterChargeMode();
            }
        }

        if (!pressed && was_pressed) {
            const int64_t held = now - press_start;
            if (!long_fired && charge_mode_ && held < kShortPressMaxMs) {
                (void)exitChargeMode();
            }
        }

        was_pressed = pressed;
        vTaskDelay(pdMS_TO_TICKS(kPollMs));
    }
}

Status PowerModeService::enterChargeMode() {
    if (charge_mode_) {
        return Status::success();
    }
    if (wifi_.provisioning()) {
        Log::warn(kTag, "charge mode unavailable during provisioning");
        return Error::InvalidState;
    }

    if (module_hooks_.stop_all != nullptr) {
        const Status status = module_hooks_.stop_all(module_hooks_.ctx);
        if (!status.ok()) {
            Log::warn(kTag, "module stop failed: %s", status.message());
        }
    }

    const Status wifi_status = wifi_.stopRadio();
    if (!wifi_status.ok()) {
        Log::warn(kTag, "wifi stop failed: %s", wifi_status.message());
    }

    (void)battery_.setSampleInterval(kChargeSampleMs);
    display_.setChargeMode(true);
    charge_mode_ = true;
    publishMode(true);
    Log::info(kTag, "charge mode — WiFi off, OLED charge display");
    return Status::success();
}

Status PowerModeService::exitChargeMode() {
    if (!charge_mode_) {
        return Status::success();
    }

    display_.setChargeMode(false);
    charge_mode_ = false;
    publishMode(false);

    const Status wifi_status = wifi_.resumeSta();
    if (!wifi_status.ok()) {
        Log::warn(kTag, "wifi resume failed: %s", wifi_status.message());
    }

    if (module_hooks_.start_all != nullptr) {
        const Status status = module_hooks_.start_all(module_hooks_.ctx);
        if (!status.ok()) {
            Log::warn(kTag, "module start failed: %s", status.message());
        }
    }

    (void)battery_.setSampleInterval(kNormalSampleMs);
    Log::info(kTag, "normal mode restored");
    return Status::success();
}

void PowerModeService::publishMode(bool charge) {
    const ChangedPayload payload = {
        .charge_mode = static_cast<uint8_t>(charge ? 1 : 0),
    };
    (void)events_.publish(Event::make(EventId::PowerModeChanged, payload));
}

}  // namespace gallus::services
