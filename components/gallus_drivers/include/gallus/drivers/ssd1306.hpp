#pragma once

#include <cstddef>
#include <cstdint>

#include "gallus/drivers/i2c_bus.hpp"
#include "gallus/error.hpp"

/// @file ssd1306.hpp
/// @brief SSD1306 128x64 monochrome OLED driver.
///
/// Owns a 1 KB page-format framebuffer (8 pages x 128 columns, bit 0 =
/// top pixel of the page) that matches the tools/gallus_image_gen.py
/// output, so drawing a generated frame is a memcpy. Callers mutate
/// the framebuffer, then flush() pushes it over I2C.
///
/// invert() uses the panel's hardware invert command — a single byte —
/// which is how the splash flicker effect is done without a second
/// buffer.

namespace gallus::drivers {

class Ssd1306 {
public:
    static constexpr int kWidth = 128;
    static constexpr int kHeight = 64;
    static constexpr size_t kBufferBytes = kWidth * kHeight / 8;
    static constexpr uint8_t kDefaultAddress = 0x3C;

    Ssd1306() = default;
    Ssd1306(const Ssd1306&) = delete;
    Ssd1306& operator=(const Ssd1306&) = delete;

    /// Attach to @p bus at @p address and run the init sequence.
    Status init(I2cBus& bus, uint8_t address = kDefaultAddress);

    [[nodiscard]] bool ready() const { return dev_ != nullptr; }

    // Framebuffer editing (no I2C traffic until flush()).
    void clear();
    void fill();
    void drawPixel(int x, int y, bool on);

    /// Copy a full page-format frame (kBufferBytes) into the buffer.
    void drawFrame(const uint8_t* frame);

    /// Direct framebuffer access for higher-level renderers (fonts).
    [[nodiscard]] uint8_t* buffer() { return buffer_; }

    /// Push the framebuffer to the panel.
    Status flush();

    // Hardware commands (take effect immediately).
    Status setInverted(bool inverted);
    Status setDisplayOn(bool on);
    Status setContrast(uint8_t value);

private:
    Status sendCommand(uint8_t cmd);
    Status sendCommands(const uint8_t* cmds, size_t len);

    I2cBus* bus_ = nullptr;
    i2c_master_dev_handle_t dev_ = nullptr;
    uint8_t buffer_[kBufferBytes] = {};
};

}  // namespace gallus::drivers
