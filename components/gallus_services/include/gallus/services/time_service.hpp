#pragma once

#include "gallus/error.hpp"
#include "gallus/event_bus.hpp"

/// @file time_service.hpp
/// @brief Wall-clock time via SNTP.
///
/// Starts SNTP on the first WiFi connection and publishes TimeSynced
/// when the system clock is set. Until then the clock reads as 1970.

namespace gallus::services {

class TimeService {
public:
    explicit TimeService(EventBus& events) : events_(events) {}
    TimeService(const TimeService&) = delete;
    TimeService& operator=(const TimeService&) = delete;

    /// Subscribe to WiFiConnected; SNTP starts on first connect.
    Status init();

    [[nodiscard]] bool synced() const { return synced_; }

private:
    static void onWifiConnected(const Event& event, void* ctx);
    static void onSntpSync(struct timeval* tv);
    void startSntp();

    EventBus& events_;
    bool started_ = false;
    bool synced_ = false;
};

}  // namespace gallus::services
