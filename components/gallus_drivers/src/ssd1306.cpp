#include "gallus/drivers/ssd1306.hpp"

#include <cstring>

#include "gallus/log.hpp"

namespace gallus::drivers {

namespace {

constexpr const char* kTag = "SSD1306";

// Control bytes prefixing every I2C transfer.
constexpr uint8_t kCtrlCommand = 0x00;  // Co=0, D/C#=0
constexpr uint8_t kCtrlData = 0x40;     // Co=0, D/C#=1

// Panel init sequence for a 128x64 module.
constexpr uint8_t kInitSequence[] = {
    0xAE,        // display off
    0xD5, 0x80,  // clock divide ratio / oscillator
    0xA8, 0x3F,  // multiplex ratio = 64
    0xD3, 0x00,  // display offset = 0
    0x40,        // start line = 0
    0x8D, 0x14,  // charge pump on
    0x20, 0x00,  // memory mode = horizontal
    0xA1,        // segment remap (column 127 -> SEG0)
    0xC8,        // COM scan direction remapped
    0xDA, 0x12,  // COM pins config
    0x81, 0xCF,  // contrast
    0xD9, 0xF1,  // pre-charge period
    0xDB, 0x40,  // VCOMH deselect level
    0xA4,        // resume to RAM content
    0xA6,        // normal (non-inverted)
    0xAF,        // display on
};

}  // namespace

Status Ssd1306::init(I2cBus& bus, uint8_t address) {
    if (dev_ != nullptr) {
        return Error::InvalidState;
    }
    bus_ = &bus;

    Status status = bus_->addDevice(address, &dev_);
    if (!status.ok()) {
        dev_ = nullptr;
        return status;
    }

    status = sendCommands(kInitSequence, sizeof(kInitSequence));
    if (!status.ok()) {
        Log::error(kTag, "init sequence failed: %s", status.message());
        return status;
    }

    clear();
    status = flush();
    if (status.ok()) {
        Log::info(kTag, "OLED ready at 0x%02X", address);
    }
    return status;
}

void Ssd1306::clear() { std::memset(buffer_, 0x00, sizeof(buffer_)); }

void Ssd1306::fill() { std::memset(buffer_, 0xFF, sizeof(buffer_)); }

void Ssd1306::drawPixel(int x, int y, bool on) {
    if (x < 0 || x >= kWidth || y < 0 || y >= kHeight) {
        return;
    }
    const size_t index = (y / 8) * kWidth + x;
    const uint8_t mask = 1 << (y % 8);
    if (on) {
        buffer_[index] |= mask;
    } else {
        buffer_[index] &= ~mask;
    }
}

void Ssd1306::drawFrame(const uint8_t* frame) {
    if (frame != nullptr) {
        std::memcpy(buffer_, frame, sizeof(buffer_));
    }
}

Status Ssd1306::flush() {
    if (dev_ == nullptr) {
        return Error::InvalidState;
    }

    // Reset the column/page address window to full screen.
    const uint8_t window[] = {
        0x21, 0x00, kWidth - 1,       // column range
        0x22, 0x00, (kHeight / 8) - 1,  // page range
    };
    GALLUS_RETURN_IF_ERROR(sendCommands(window, sizeof(window)));

    // Stream the framebuffer prefixed with the data control byte.
    uint8_t packet[1 + kBufferBytes];
    packet[0] = kCtrlData;
    std::memcpy(packet + 1, buffer_, kBufferBytes);
    return bus_->write(dev_, packet, sizeof(packet));
}

Status Ssd1306::setInverted(bool inverted) {
    return sendCommand(inverted ? 0xA7 : 0xA6);
}

Status Ssd1306::setDisplayOn(bool on) {
    return sendCommand(on ? 0xAF : 0xAE);
}

Status Ssd1306::setContrast(uint8_t value) {
    const uint8_t cmd[] = {0x81, value};
    return sendCommands(cmd, sizeof(cmd));
}

Status Ssd1306::sendCommand(uint8_t cmd) {
    const uint8_t packet[] = {kCtrlCommand, cmd};
    return bus_->write(dev_, packet, sizeof(packet));
}

Status Ssd1306::sendCommands(const uint8_t* cmds, size_t len) {
    // Each command byte is sent as a control+command pair.
    for (size_t i = 0; i < len; ++i) {
        GALLUS_RETURN_IF_ERROR(sendCommand(cmds[i]));
    }
    return Status::success();
}

}  // namespace gallus::drivers
