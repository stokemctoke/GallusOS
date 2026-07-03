#pragma once

#include <cstdint>

#include "esp_http_server.h"

#include "gallus/error.hpp"
#include "gallus/event_bus.hpp"
#include "gallus/services/rest_service.hpp"

/// @file ota_service.hpp
/// @brief Over-the-air firmware updates.
///
/// v1 scope: manual upload. A new firmware image is POSTed to
/// /api/v1/ota/upload and streamed straight into the inactive OTA
/// partition; on success the device reboots into it. Progress is
/// reported via OTAStarted / OTAProgress / OTAFinished events (which
/// the WebUI relays over WebSocket).
///
/// Rollback: the bootloader boots the new image in a "pending verify"
/// state. confirmHealthy() (called once the system is up and happy)
/// marks it valid; if the device crash-loops before that, the
/// bootloader rolls back to the previous image automatically.

namespace gallus::services {

class OtaService {
public:
    /// Payload carried by EventId::OTAProgress.
    struct ProgressPayload {
        uint32_t received;
        uint32_t total;
        uint8_t percent;
    };

    /// Payload carried by EventId::OTAFinished.
    struct FinishedPayload {
        uint8_t success;
    };

    OtaService(EventBus& events, RestService& rest)
        : events_(events), rest_(rest) {}
    OtaService(const OtaService&) = delete;
    OtaService& operator=(const OtaService&) = delete;

    /// Register the OTA upload route.
    Status init();

    /// Mark the running image valid so the bootloader keeps it. Call
    /// after the system reaches a known-good state. No-op when the
    /// image is already valid or rollback is disabled.
    void confirmHealthy();

    [[nodiscard]] bool inProgress() const { return in_progress_; }

private:
    static esp_err_t uploadHandler(httpd_req_t* req);

    EventBus& events_;
    RestService& rest_;
    volatile bool in_progress_ = false;
};

}  // namespace gallus::services
