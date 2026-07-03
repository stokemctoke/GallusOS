#pragma once

#include <cstdint>
#include <cstddef>

#include "driver/i2c_master.h"

#include "gallus/error.hpp"

/// @file i2c_bus.hpp
/// @brief Shared I2C master bus (ESP-IDF 5.x i2c_master API).
///
/// One bus instance is created per physical I2C port and shared by
/// every device driver on it (display, sensors, ...). Device drivers
/// add themselves with addDevice() and talk through the returned
/// handle.

namespace gallus::drivers {

class I2cBus {
public:
    I2cBus() = default;
    I2cBus(const I2cBus&) = delete;
    I2cBus& operator=(const I2cBus&) = delete;

    /// Initialise the bus on @p sda / @p scl at @p freq_hz.
    Status init(int sda, int scl, uint32_t freq_hz = 400000);

    /// Register a 7-bit device address; fills @p out_handle.
    Status addDevice(uint8_t address, i2c_master_dev_handle_t* out_handle);

    /// Blocking write of @p len bytes to @p dev.
    Status write(i2c_master_dev_handle_t dev, const uint8_t* data,
                 size_t len);

    /// Probe whether @p address acknowledges on the bus.
    [[nodiscard]] bool probe(uint8_t address) const;

    [[nodiscard]] bool ready() const { return bus_ != nullptr; }

private:
    i2c_master_bus_handle_t bus_ = nullptr;
    uint32_t freq_hz_ = 400000;
};

}  // namespace gallus::drivers
