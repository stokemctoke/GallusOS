#pragma once

#include <cstddef>
#include <cstdint>

/// @file board.hpp
/// @brief Board definition for the Seeed Studio XIAO ESP32-C5.
///
/// The HAL is the ONLY component that knows which chip/board it runs on.
/// Per-board pin facts live here; everything above consumes these
/// constants through driver/service interfaces.

namespace gallus::hal {

/// Fixed hardware facts for the XIAO ESP32-C5.
namespace board {

/// Battery voltage ADC input (behind a 1:2 divider, 2x 100K).
inline constexpr int kPinBatteryAdc = 6;

/// Battery measurement circuit enable. Drive HIGH before sampling
/// kPinBatteryAdc, LOW afterwards to save power.
inline constexpr int kPinBatteryMeasureEnable = 26;

/// Onboard user LED (yellow).
inline constexpr int kPinUserLed = 27;

/// BOOT button.
inline constexpr int kPinBootButton = 28;

/// Default I2C pins (D4/D5) — claimed by the display service in Phase 5.
inline constexpr int kPinI2cSda = 23;
inline constexpr int kPinI2cScl = 24;

/// Highest GPIO number present on this board.
inline constexpr int kMaxGpioNumber = 28;

/// Every GPIO physically usable on the XIAO ESP32-C5:
/// edge pins D0-D10, the four JTAG pads on the back (2-5), and the
/// system pins (battery, LED, boot button).
inline constexpr int kValidGpios[] = {
    0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 23, 24, 25, 26, 27, 28,
};

/// Pins the framework protects absolutely — modules can never claim
/// these, not even with a user override.
inline constexpr int kCriticalPins[] = {
    kPinBatteryAdc,
    kPinBatteryMeasureEnable,
    kPinBootButton,
};

/// @return true when @p pin exists on this board.
constexpr bool isValidGpio(int pin) {
    for (int valid : kValidGpios) {
        if (valid == pin) {
            return true;
        }
    }
    return false;
}

/// @return true when @p pin is critical (never claimable).
constexpr bool isCriticalPin(int pin) {
    for (int critical : kCriticalPins) {
        if (critical == pin) {
            return true;
        }
    }
    return false;
}

}  // namespace board

}  // namespace gallus::hal
