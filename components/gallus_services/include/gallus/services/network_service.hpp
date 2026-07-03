#pragma once

#include "gallus/error.hpp"
#include "gallus/event_bus.hpp"
#include "gallus/services/config_service.hpp"

/// @file network_service.hpp
/// @brief Network discovery (mDNS).
///
/// Announces the device as <hostname>.local (config key
/// network/hostname, defaulting to gallus-XXXX from the MAC) with an
/// _http._tcp service, once WiFi connects.

namespace gallus::services {

class NetworkService {
public:
    NetworkService(ConfigService& config, EventBus& events)
        : config_(config), events_(events) {}
    NetworkService(const NetworkService&) = delete;
    NetworkService& operator=(const NetworkService&) = delete;

    /// Subscribe to WiFiConnected; mDNS starts on first connect.
    Status init();

    [[nodiscard]] const char* hostname() const { return hostname_; }

private:
    static void onWifiConnected(const Event& event, void* ctx);
    void startMdns();

    ConfigService& config_;
    EventBus& events_;
    char hostname_[32] = {};
    bool mdns_started_ = false;
};

}  // namespace gallus::services
