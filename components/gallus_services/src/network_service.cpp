#include "gallus/services/network_service.hpp"

#include <cstdio>

#include "esp_mac.h"
#include "mdns.h"

#include "gallus/log.hpp"

namespace gallus::services {

namespace {
constexpr const char* kTag = "Network";
}

Status NetworkService::init() {
    char fallback[32];
    uint8_t mac[6] = {};
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    snprintf(fallback, sizeof(fallback), "gallus-%02x%02x", mac[4], mac[5]);
    (void)config_.getString("network", "hostname", hostname_,
                            sizeof(hostname_), fallback);

    return events_
        .subscribe(EventId::WiFiConnected, &NetworkService::onWifiConnected,
                   this)
        .status();
}

void NetworkService::onWifiConnected(const Event& /*event*/, void* ctx) {
    auto* self = static_cast<NetworkService*>(ctx);
    if (!self->mdns_started_) {
        self->startMdns();
    }
}

void NetworkService::startMdns() {
    esp_err_t err = mdns_init();
    if (err != ESP_OK) {
        Log::error(kTag, "mdns init failed: %s", esp_err_to_name(err));
        return;
    }

    (void)mdns_hostname_set(hostname_);
    (void)mdns_instance_name_set("GallusOS");
    (void)mdns_service_add(nullptr, "_http", "_tcp", 80, nullptr, 0);

    mdns_started_ = true;
    Log::info(kTag, "reachable at http://%s.local", hostname_);
}

}  // namespace gallus::services
