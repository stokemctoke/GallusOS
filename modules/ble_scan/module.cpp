/// BLE Scan — on-demand Bluetooth Low Energy device survey.
///
/// Idle by default: the dashboard's "Scan now" drives GET
/// /api/v1/ble/scan directly. When period_ms > 0 this module also runs
/// a periodic background scan and logs what it finds. The heavy NimBLE
/// stack is brought up and torn down inside BleService::scan(), so this
/// module costs nothing at rest.

#include <cstring>

#include "gallus/log.hpp"
#include "gallus/sdk/module.hpp"
#include "gallus/services/ble_service.hpp"

namespace {

constexpr const char* kTag = "ble_scan";

class BleScanModule : public gallus::sdk::Module {
public:
    gallus::Status start() override {
        period_ms_ = ctx().config.getInt("ble_scan", "period_ms", 0);
        duration_ms_ = ctx().config.getInt("ble_scan", "duration_ms", 3000);

        if (period_ms_ <= 0) {
            gallus::Log::info(kTag, "ready — on-demand (period_ms=0)");
            return gallus::Status::success();
        }

        auto job = ctx().scheduler.every(
            static_cast<uint32_t>(period_ms_), &BleScanModule::tick, this,
            gallus::Priority::Background);
        if (!job.ok()) {
            return job.status();
        }
        job_ = job.value();
        return gallus::Status::success();
    }

    gallus::Status stop() override {
        if (job_.valid()) {
            (void)ctx().scheduler.cancel(job_);
            job_ = {};
        }
        return gallus::Status::success();
    }

private:
    static void tick(void* arg) {
        auto* self = static_cast<BleScanModule*>(arg);

        gallus::services::BleService::BleRecord devices
            [gallus::services::BleService::kMaxScanResults];
        const auto found = self->ctx().ble.scan(
            devices, sizeof(devices) / sizeof(devices[0]),
            static_cast<uint32_t>(self->duration_ms_));
        if (!found.ok()) {
            gallus::Log::warn(kTag, "scan failed: %s", found.message());
            return;
        }
        if (found.value() == 0) {
            gallus::Log::info(kTag, "no BLE devices found");
            return;
        }

        gallus::Log::info(kTag, "found %u device(s)",
                          static_cast<unsigned>(found.value()));
        for (size_t i = 0; i < found.value(); ++i) {
            const auto& d = devices[i];
            gallus::Log::info(kTag, "  %02X:%02X:%02X:%02X:%02X:%02X  rssi=%d%s%s",
                              d.addr[0], d.addr[1], d.addr[2], d.addr[3],
                              d.addr[4], d.addr[5], static_cast<int>(d.rssi),
                              d.name[0] ? "  " : "", d.name);
        }
    }

    gallus::JobHandle job_;
    int32_t period_ms_ = 0;
    int32_t duration_ms_ = 3000;
};

}  // namespace

GALLUS_MODULE(BleScanModule, ble_scan)
