#include "gallus/services/wifi_service.hpp"

#include <cstring>

#include "gallus/log.hpp"

namespace gallus::services {

namespace {
constexpr const char* kTag = "WiFi";
}

Status WifiService::init() {
    initialized_ = true;
    Log::info(kTag, "host stub — radio not started");
    return Status::success();
}

Status WifiService::start() { return Status::success(); }

Status WifiService::stopRadio() {
    radio_stopped_ = true;
    return Status::success();
}

Status WifiService::resumeSta() {
    radio_stopped_ = false;
    return Status::success();
}

Status WifiService::reconnectSta() { return Status::success(); }

Result<size_t> WifiService::scan(ApRecord* out, size_t max, ScanBand bands) {
    if (!initialized_ || out == nullptr || max == 0) {
        return Error::InvalidArg;
    }

    static const ApRecord kMockAps[] = {
        {.ssid = "GallusLab-2G",
         .bssid = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0x01},
         .rssi = -42,
         .channel = 6,
         .band_ghz = 2},
        {.ssid = "GallusLab-5G",
         .bssid = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0x02},
         .rssi = -55,
         .channel = 36,
         .band_ghz = 5},
        {.ssid = "Neighbor",
         .bssid = {0x11, 0x22, 0x33, 0x44, 0x55, 0x66},
         .rssi = -70,
         .channel = 11,
         .band_ghz = 2},
    };

    size_t count = 0;
    for (const ApRecord& ap : kMockAps) {
        if (count >= max) {
            break;
        }
        if (bands == ScanBand::Band2G && ap.band_ghz != 2) {
            continue;
        }
        if (bands == ScanBand::Band5G && ap.band_ghz != 5) {
            continue;
        }
        std::memcpy(&out[count], &ap, sizeof(ApRecord));
        ++count;
    }
    return count;
}

}  // namespace gallus::services
