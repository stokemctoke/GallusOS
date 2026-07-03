/// WiFi Scan — dual-band survey via WifiService.

#include <cstring>

#include "gallus/log.hpp"
#include "gallus/sdk/module.hpp"
#include "gallus/services/wifi_service.hpp"

namespace {

constexpr const char* kTag = "wifi_scan";

gallus::services::WifiService::ScanBand parseBand(const char* text) {
    if (text == nullptr) {
        return gallus::services::WifiService::ScanBand::Both;
    }
    if (std::strcmp(text, "2.4") == 0 || std::strcmp(text, "2g") == 0) {
        return gallus::services::WifiService::ScanBand::Band2G;
    }
    if (std::strcmp(text, "5") == 0 || std::strcmp(text, "5g") == 0) {
        return gallus::services::WifiService::ScanBand::Band5G;
    }
    return gallus::services::WifiService::ScanBand::Both;
}

class WifiScanModule : public gallus::sdk::Module {
public:
    gallus::Status start() override {
        period_ms_ = ctx().config.getInt("wifi_scan", "period_ms", 60000);

        char band_text[8] = {};
        (void)ctx().config.getString("wifi_scan", "band", band_text,
                                     sizeof(band_text), "both");
        band_ = parseBand(band_text);

        auto job = ctx().scheduler.every(
            static_cast<uint32_t>(period_ms_), &WifiScanModule::tick, this,
            gallus::Priority::Background);
        if (!job.ok()) {
            return job.status();
        }
        job_ = job.value();
        tick(this);
        return gallus::Status::success();
    }

    gallus::Status stop() override {
        (void)ctx().scheduler.cancel(job_);
        job_ = {};
        return gallus::Status::success();
    }

private:
    static void tick(void* arg) {
        auto* self = static_cast<WifiScanModule*>(arg);

        gallus::services::WifiService::ApRecord records
            [gallus::services::WifiService::kMaxScanResults];
        const auto found =
            self->ctx().wifi.scan(records, sizeof(records) / sizeof(records[0]),
                                  self->band_);
        if (!found.ok()) {
            gallus::Log::warn(kTag, "scan failed: %s", found.message());
            return;
        }

        if (found.value() == 0) {
            gallus::Log::info(kTag, "no networks found");
            return;
        }

        gallus::Log::info(kTag, "found %u network(s)",
                          static_cast<unsigned>(found.value()));
        for (size_t i = 0; i < found.value(); ++i) {
            const auto& ap = records[i];
            gallus::Log::info(kTag, "  %s  rssi=%d ch=%u %uGHz",
                              ap.ssid, static_cast<int>(ap.rssi),
                              static_cast<unsigned>(ap.channel),
                              static_cast<unsigned>(ap.band_ghz));
        }
    }

    gallus::JobHandle job_;
    int32_t period_ms_ = 60000;
    gallus::services::WifiService::ScanBand band_ =
        gallus::services::WifiService::ScanBand::Both;
};

}  // namespace

GALLUS_MODULE(WifiScanModule, wifi_scan)
