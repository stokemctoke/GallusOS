#include "gallus/services/time_service.hpp"

#include <ctime>

#include "esp_netif_sntp.h"

#include "gallus/log.hpp"

namespace gallus::services {

namespace {
constexpr const char* kTag = "Time";

// The SNTP callback carries no context pointer, so the service keeps
// the one live instance here (set in init).
TimeService* s_instance = nullptr;
}  // namespace

Status TimeService::init() {
    s_instance = this;
    return events_
        .subscribe(EventId::WiFiConnected, &TimeService::onWifiConnected,
                   this)
        .status();
}

void TimeService::onWifiConnected(const Event& /*event*/, void* ctx) {
    auto* self = static_cast<TimeService*>(ctx);
    if (!self->started_) {
        self->startSntp();
    }
}

void TimeService::startSntp() {
    esp_sntp_config_t config = ESP_NETIF_SNTP_DEFAULT_CONFIG("pool.ntp.org");
    config.sync_cb = &TimeService::onSntpSync;

    const esp_err_t err = esp_netif_sntp_init(&config);
    if (err != ESP_OK) {
        Log::error(kTag, "sntp init failed: %s", esp_err_to_name(err));
        return;
    }
    started_ = true;
    Log::info(kTag, "sntp started (pool.ntp.org)");
}

void TimeService::onSntpSync(struct timeval* tv) {
    if (s_instance == nullptr || tv == nullptr) {
        return;
    }
    s_instance->synced_ = true;

    char stamp[32] = {};
    const time_t now = tv->tv_sec;
    struct tm tm_utc = {};
    gmtime_r(&now, &tm_utc);
    strftime(stamp, sizeof(stamp), "%Y-%m-%d %H:%M:%S", &tm_utc);
    Log::info(kTag, "clock synced: %s UTC", stamp);

    (void)s_instance->events_.publish(Event::make(EventId::TimeSynced));
}

}  // namespace gallus::services
