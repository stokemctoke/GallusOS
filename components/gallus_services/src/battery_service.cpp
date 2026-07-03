#include "gallus/services/battery_service.hpp"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "gallus/drivers/gpio.hpp"
#include "gallus/log.hpp"

namespace gallus::services {

namespace {

constexpr const char* kTag = "Battery";

// Li-ion open-circuit voltage breakpoints (mV) -> approximate charge.
// Coarse but honest; a real fuel gauge would do better.
struct Point {
    uint16_t mv;
    uint8_t pct;
};
constexpr Point kCurve[] = {
    {3300, 0},  {3600, 10}, {3700, 25}, {3750, 40}, {3800, 55},
    {3900, 70}, {4000, 85}, {4100, 95}, {4200, 100},
};

}  // namespace

Status BatteryService::init() {
    if (initialized_) {
        return Error::InvalidState;
    }

    // The enable pin (GPIO26) is a board-critical pin owned by this
    // core service — drive it straight through the driver.
    GALLUS_RETURN_IF_ERROR(
        drivers::GpioDriver::configure(enable_pin_, drivers::PinMode::Output));
    GALLUS_RETURN_IF_ERROR(drivers::GpioDriver::write(enable_pin_, false));

    GALLUS_RETURN_IF_ERROR(adc_.init(adc_pin_));

    initialized_ = true;
    Log::info(kTag, "ready (adc=GPIO%d enable=GPIO%d)", adc_pin_, enable_pin_);
    return Status::success();
}

Status BatteryService::start(uint32_t interval_ms) {
    if (!initialized_) {
        return Error::InvalidState;
    }
    sample();  // immediate first reading
    auto job = scheduler_.every(interval_ms, &BatteryService::sampleJob, this,
                                Priority::Background);
    return job.status();
}

void BatteryService::sampleJob(void* ctx) {
    static_cast<BatteryService*>(ctx)->sample();
}

void BatteryService::sample() {
    // Enable the measurement circuit, let it settle, read, disable.
    (void)drivers::GpioDriver::write(enable_pin_, true);
    vTaskDelay(pdMS_TO_TICKS(10));

    Result<int> mv = adc_.readMillivolts(16);

    (void)drivers::GpioDriver::write(enable_pin_, false);

    if (!mv.ok()) {
        Log::warn(kTag, "ADC read failed: %s", mv.message());
        return;
    }

    // The pin sees half the battery voltage (1:2 divider).
    const uint16_t battery_mv = static_cast<uint16_t>(mv.value() * 2);
    const uint8_t pct = estimatePercent(battery_mv);

    last_mv_ = battery_mv;
    last_pct_ = pct;

    const ChangedPayload payload = {
        .millivolts = battery_mv,
        .percent = pct,
        .charging = 0,
    };
    (void)events_.publish(Event::make(EventId::BatteryChanged, payload));
    Log::debug(kTag, "%u mV (%u%%)", battery_mv, pct);
}

uint8_t BatteryService::estimatePercent(uint16_t mv) {
    if (mv <= kCurve[0].mv) {
        return 0;
    }
    const size_t n = sizeof(kCurve) / sizeof(kCurve[0]);
    if (mv >= kCurve[n - 1].mv) {
        return 100;
    }
    for (size_t i = 1; i < n; ++i) {
        if (mv < kCurve[i].mv) {
            const Point& lo = kCurve[i - 1];
            const Point& hi = kCurve[i];
            const int span_mv = hi.mv - lo.mv;
            const int span_pct = hi.pct - lo.pct;
            return static_cast<uint8_t>(
                lo.pct + (mv - lo.mv) * span_pct / span_mv);
        }
    }
    return 100;
}

}  // namespace gallus::services
