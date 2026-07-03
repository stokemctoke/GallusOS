#pragma once

#include <cstdint>

#include "gallus/error.hpp"

/// @file adc.hpp
/// @brief One-shot ADC reader with calibrated millivolt output.
///
/// Wraps the ESP-IDF oneshot ADC + curve-fitting calibration. Used by
/// BatteryService to read the XIAO ESP32-C5 battery divider on GPIO6.

namespace gallus::drivers {

class Adc {
public:
    Adc() = default;
    Adc(const Adc&) = delete;
    Adc& operator=(const Adc&) = delete;
    ~Adc();

    /// Configure the ADC channel that @p gpio maps to (12-bit, 12 dB
    /// attenuation for the full ~0-3.3 V range).
    Status init(int gpio);

    /// Averaged calibrated reading in millivolts (@p samples reads).
    Result<int> readMillivolts(int samples = 16);

private:
    void* unit_ = nullptr;   // adc_oneshot_unit_handle_t
    void* cali_ = nullptr;   // adc_cali_handle_t
    int channel_ = -1;
    bool calibrated_ = false;
};

}  // namespace gallus::drivers
