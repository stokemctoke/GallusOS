#pragma once

#include <cstddef>
#include <cstdint>

#include "gallus/drivers/i2c_bus.hpp"
#include "gallus/error.hpp"

/// @file i2c_service.hpp
/// @brief Shared I2C bus for the board.
///
/// One bus instance per board, shared by the display driver and any
/// module that needs to probe or talk to I2C devices.

namespace gallus::services {

class I2cService {
public:
    static constexpr size_t kMaxScanResults = 16;

    I2cService() = default;
    I2cService(const I2cService&) = delete;
    I2cService& operator=(const I2cService&) = delete;

    /// Initialise the bus on @p sda / @p scl.
    Status init(int sda, int scl, uint32_t freq_hz = 400000);

    [[nodiscard]] bool ready() const { return ready_; }

    /// Raw bus access for device drivers (display, sensors, …).
    drivers::I2cBus& bus() { return bus_; }

    /// Scan 7-bit addresses 0x03–0x77. Writes found addresses to @p out
    /// (up to @p max) and returns the count.
    Result<size_t> scan(uint8_t* out, size_t max) const;

private:
    drivers::I2cBus bus_;
    bool ready_ = false;
};

}  // namespace gallus::services
