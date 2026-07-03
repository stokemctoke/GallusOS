#pragma once

#include <cstdint>

#include "gallus/error.hpp"

/// @file gpio.hpp
/// @brief GPIO driver — thin, stateless wrapper over the ESP-IDF GPIO
/// driver.
///
/// Consumed by GpioService, which adds the reservation/ownership
/// policy. Nothing above the service layer includes this header.

namespace gallus::drivers {

enum class PinMode : uint8_t {
    Disabled,
    Input,
    InputPullUp,
    InputPullDown,
    Output,
    OutputOpenDrain,
};

class GpioDriver {
public:
    GpioDriver() = delete;  // stateless: all members static

    /// Configure @p pin direction and pulls.
    static Status configure(int pin, PinMode mode);

    /// Drive an output pin.
    static Status write(int pin, bool level);

    /// Sample an input pin.
    static Result<bool> read(int pin);

    /// Return @p pin to its power-on state.
    static Status reset(int pin);
};

}  // namespace gallus::drivers
