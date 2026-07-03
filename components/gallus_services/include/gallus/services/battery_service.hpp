#pragma once

#include <cstdint>

#include "gallus/drivers/adc.hpp"
#include "gallus/error.hpp"
#include "gallus/event_bus.hpp"
#include "gallus/scheduler.hpp"

/// @file battery_service.hpp
/// @brief Battery monitoring for the XIAO ESP32-C5.
///
/// Reads the onboard divider: drive the measure-enable pin (GPIO26)
/// high, sample the ADC pin (GPIO6), multiply by 2 for the 1:2
/// divider, then release the enable pin to save power. Publishes
/// BatteryChanged on a scheduled interval.
///
/// Both pins are board-critical system pins, so this core service
/// drives them through the GPIO driver directly (the reservation
/// manager exists to arbitrate MODULE pin requests; these pins are
/// never assignable to modules).
///
/// Charging state is NOT exposed: the XIAO has no charge-status GPIO
/// (the SGM40567 only drives the onboard LED), so reporting it would
/// be a guess. See the hardware notes in the spec.

namespace gallus::services {

class BatteryService {
public:
    /// Payload carried by EventId::BatteryChanged.
    struct ChangedPayload {
        uint16_t millivolts;
        uint8_t percent;
        uint8_t charging;  // always 0 — reserved, no HW support
    };

    BatteryService(EventBus& events, Scheduler& scheduler, int adc_pin,
                   int enable_pin)
        : events_(events),
          scheduler_(scheduler),
          adc_pin_(adc_pin),
          enable_pin_(enable_pin) {}
    BatteryService(const BatteryService&) = delete;
    BatteryService& operator=(const BatteryService&) = delete;

    /// Configure the enable pin and the ADC.
    Status init();

    /// Take one reading now and schedule periodic sampling.
    Status start(uint32_t interval_ms = 30000);

    /// Change the sampling interval (cancels the previous periodic job).
    Status setSampleInterval(uint32_t interval_ms);

    /// Last reading in millivolts (battery terminal, post-divider math).
    [[nodiscard]] uint16_t millivolts() const { return last_mv_; }
    [[nodiscard]] uint8_t percent() const { return last_pct_; }

private:
    static void sampleJob(void* ctx);
    void sample();
    static uint8_t estimatePercent(uint16_t mv);

    EventBus& events_;
    Scheduler& scheduler_;
    drivers::Adc adc_;
    int adc_pin_;
    int enable_pin_;
    uint16_t last_mv_ = 0;
    uint8_t last_pct_ = 0;
    JobHandle sample_job_ = {};
    bool initialized_ = false;
    bool sampling_ = false;
};

}  // namespace gallus::services
