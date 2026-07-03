#pragma once

#include <cstdint>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#include "gallus/drivers/i2c_bus.hpp"
#include "gallus/drivers/ssd1306.hpp"
#include "gallus/error.hpp"
#include "gallus/event_bus.hpp"

/// @file display_service.hpp
/// @brief The OLED companion display.
///
/// Owns the I2C bus and the SSD1306 panel. Plays the boot splash
/// sequence (logo -> pure gallus -> gallus os, with a hardware-invert
/// flicker between frames), then shows a live status screen driven by
/// framework events (WiFi, battery, module changes). It never tries to
/// replicate the WebUI.
///
/// All panel access is funnelled through a single worker task so I2C
/// traffic is serialized; event handlers only update cached state and
/// request a redraw.

namespace gallus::services {

class DisplayService {
public:
    DisplayService(EventBus& events, int sda, int scl)
        : events_(events), sda_(sda), scl_(scl) {}
    DisplayService(const DisplayService&) = delete;
    DisplayService& operator=(const DisplayService&) = delete;

    /// Bring up I2C + the panel. Returns NotFound (gracefully) when no
    /// display is attached, so a headless board still boots.
    Status init();

    /// Play the boot splash sequence (blocking, ~2 s). Safe to skip.
    void playSplash();

    /// Subscribe to framework events and start the status screen.
    Status start();

    [[nodiscard]] bool present() const { return present_; }

private:
    enum class Screen : uint8_t { Splash, Status };

    struct StatusState {
        bool wifi_connected = false;
        uint8_t ip[4] = {};
        uint8_t battery_pct = 0;
        bool battery_valid = false;
        char module[24] = {};
    };

    static void onEvent(const Event& event, void* ctx);
    void flickerTo(const uint8_t* frame, int flashes);
    void renderStatus();
    void drawText(int x, int y, const char* text, bool on = true);

    EventBus& events_;
    drivers::I2cBus bus_;
    drivers::Ssd1306 panel_;
    int sda_;
    int scl_;
    StatusState status_ = {};
    SemaphoreHandle_t mutex_ = nullptr;
    bool present_ = false;
    bool status_active_ = false;
};

}  // namespace gallus::services
