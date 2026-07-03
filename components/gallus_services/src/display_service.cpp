#include "gallus/services/display_service.hpp"

#include <cstdio>
#include <cstring>

#include "freertos/task.h"

#include "esp_mac.h"

#include "gallus/drivers/font5x7.hpp"
#include "gallus/log.hpp"
#include "gallus/services/splash_frames.hpp"

namespace gallus::services {

namespace {

constexpr const char* kTag = "Display";

/// Battery payload mirror (must match BatteryService::ChangedPayload).
struct BatteryPayload {
    uint16_t millivolts;
    uint8_t percent;
    uint8_t charging;
};

/// WiFi payload mirror (must match WifiService::ConnectedPayload).
struct WifiPayload {
    uint8_t ip[4];
};

/// Module payload mirror (must match ModuleManager::ModuleEventPayload).
struct ModulePayload {
    char name[24];
};

void delayMs(uint32_t ms) { vTaskDelay(pdMS_TO_TICKS(ms)); }

}  // namespace

Status DisplayService::init() {
    mutex_ = xSemaphoreCreateMutex();
    if (mutex_ == nullptr) {
        return Error::NoMemory;
    }

    if (!i2c_.ready()) {
        return Error::InvalidState;
    }

    // Probe first so a headless board degrades gracefully.
    if (!i2c_.bus().probe(drivers::Ssd1306::kDefaultAddress)) {
        Log::warn(kTag, "no SSD1306 at 0x%02X — running headless",
                  drivers::Ssd1306::kDefaultAddress);
        return Error::NotFound;
    }

    Status status = panel_.init(i2c_.bus());
    if (!status.ok()) {
        return status;
    }
    present_ = true;
    return Status::success();
}

void DisplayService::playSplash() {
    if (!present_) {
        return;
    }
    flickerPair(assets::kLogo, assets::kLogoinv, 2);
    delayMs(650);
    flickerTo(assets::kPuregallus, 2);
    delayMs(650);
    flickerTo(assets::kGallusos, 3);
    delayMs(800);
}

/// Draw a frame, then flash the panel via hardware invert a few times
/// for a CRT-style flicker as it settles. One I2C byte per flip — no
/// second framebuffer needed.
void DisplayService::flickerTo(const uint8_t* frame, int flashes) {
    xSemaphoreTake(mutex_, portMAX_DELAY);
    panel_.drawFrame(frame);
    (void)panel_.flush();
    for (int i = 0; i < flashes; ++i) {
        (void)panel_.setInverted(true);
        delayMs(45);
        (void)panel_.setInverted(false);
        delayMs(60);
    }
    xSemaphoreGive(mutex_);
}

/// Alternate between two prepared frames (e.g. black-on-white and
/// white-on-black artwork) for the opening logo flicker.
void DisplayService::flickerPair(const uint8_t* light, const uint8_t* dark,
                                 int flashes) {
    xSemaphoreTake(mutex_, portMAX_DELAY);
    for (int i = 0; i < flashes; ++i) {
        panel_.drawFrame(light);
        (void)panel_.flush();
        delayMs(45);
        panel_.drawFrame(dark);
        (void)panel_.flush();
        delayMs(60);
    }
    panel_.drawFrame(light);
    (void)panel_.flush();
    xSemaphoreGive(mutex_);
}

Status DisplayService::start() {
    if (!present_) {
        return Status::success();  // headless: nothing to drive
    }

    GALLUS_RETURN_IF_ERROR(
        events_.subscribe(EventId::WiFiConnected, &onEvent, this).status());
    GALLUS_RETURN_IF_ERROR(
        events_.subscribe(EventId::WiFiDisconnected, &onEvent, this).status());
    GALLUS_RETURN_IF_ERROR(
        events_.subscribe(EventId::BatteryChanged, &onEvent, this).status());
    GALLUS_RETURN_IF_ERROR(
        events_.subscribe(EventId::ModuleStarted, &onEvent, this).status());

    status_active_ = true;
    updateHostname();
    renderStatus();
    return Status::success();
}

void DisplayService::updateHostname() {
    char fallback[24] = {};
    uint8_t mac[6] = {};
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    snprintf(fallback, sizeof(fallback), "gallus-%02x%02x", mac[4], mac[5]);
    (void)config_.getString("network", "hostname", status_.hostname,
                            sizeof(status_.hostname), fallback);
}

void DisplayService::onEvent(const Event& event, void* ctx) {
    auto* self = static_cast<DisplayService*>(ctx);

    switch (event.id) {
        case EventId::WiFiConnected:
            if (const auto* p = event.as<WifiPayload>()) {
                self->status_.wifi_connected = true;
                memcpy(self->status_.ip, p->ip, 4);
                self->updateHostname();
            }
            break;
        case EventId::WiFiDisconnected:
            self->status_.wifi_connected = false;
            break;
        case EventId::BatteryChanged:
            if (const auto* p = event.as<BatteryPayload>()) {
                self->status_.battery_pct = p->percent;
                self->status_.battery_valid = true;
            }
            break;
        case EventId::ModuleStarted:
            if (const auto* p = event.as<ModulePayload>()) {
                snprintf(self->status_.module, sizeof(self->status_.module),
                         "%s", p->name);
            }
            break;
        default:
            return;
    }
    self->renderStatus();
}

void DisplayService::renderStatus() {
    if (!status_active_) {
        return;
    }

    xSemaphoreTake(mutex_, portMAX_DELAY);
    panel_.clear();

    drawText(0, 0, "GallusOS");
    // Header underline.
    for (int x = 0; x < drivers::Ssd1306::kWidth; ++x) {
        panel_.drawPixel(x, 10, true);
    }

    char line[40];
    if (status_.wifi_connected) {
        snprintf(line, sizeof(line), "IP %u.%u.%u.%u", status_.ip[0],
                 status_.ip[1], status_.ip[2], status_.ip[3]);
        drawText(0, 16, line);
        snprintf(line, sizeof(line), "%s.local", status_.hostname);
        drawText(0, 26, line);
    } else {
        drawText(0, 16, "WiFi: connecting...");
    }

    if (status_.battery_valid) {
        snprintf(line, sizeof(line), "Bat: %u%%", status_.battery_pct);
    } else {
        snprintf(line, sizeof(line), "Bat: --");
    }
    drawText(0, 38, line);

    if (status_.module[0] != '\0') {
        snprintf(line, sizeof(line), "Mod: %s", status_.module);
        drawText(0, 50, line);
    }

    (void)panel_.flush();
    xSemaphoreGive(mutex_);
}

void DisplayService::drawText(int x, int y, const char* text, bool on) {
    for (const char* c = text; *c != '\0'; ++c) {
        const int ch = static_cast<unsigned char>(*c);
        if (ch < drivers::kFontFirstChar || ch > drivers::kFontLastChar) {
            x += drivers::kFontWidth + 1;
            continue;
        }
        const uint8_t* glyph =
            drivers::kFont5x7[ch - drivers::kFontFirstChar];
        for (int col = 0; col < drivers::kFontWidth; ++col) {
            const uint8_t bits = glyph[col];
            for (int row = 0; row < drivers::kFontHeight; ++row) {
                if (bits & (1 << row)) {
                    panel_.drawPixel(x + col, y + row, on);
                }
            }
        }
        x += drivers::kFontWidth + 1;
    }
}

}  // namespace gallus::services
