/// IEEE 802.15.4 Scan — on-demand Zigbee/Thread/Matter channel survey.
///
/// Idle by default: the dashboard's "Survey now" drives GET
/// /api/v1/ieee802154/scan directly. When period_ms > 0 this module
/// also surveys on a schedule and logs active channels. The radio is
/// brought up and torn down inside Ieee802154Service::scan(), so this
/// module costs nothing at rest.

#include "gallus/log.hpp"
#include "gallus/sdk/module.hpp"
#include "gallus/services/ieee802154_service.hpp"

namespace {

constexpr const char* kTag = "ieee802154_scan";

class Ieee802154ScanModule : public gallus::sdk::Module {
public:
    gallus::Status start() override {
        period_ms_ = ctx().config.getInt("ieee802154_scan", "period_ms", 0);
        dwell_ms_ = ctx().config.getInt("ieee802154_scan", "dwell_ms", 120);

        if (period_ms_ <= 0) {
            gallus::Log::info(kTag, "ready — on-demand (period_ms=0)");
            return gallus::Status::success();
        }

        auto job = ctx().scheduler.every(
            static_cast<uint32_t>(period_ms_), &Ieee802154ScanModule::tick,
            this, gallus::Priority::Background);
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
        auto* self = static_cast<Ieee802154ScanModule*>(arg);

        gallus::services::Ieee802154Service::ChannelRecord chans
            [gallus::services::Ieee802154Service::kChannelCount];
        const auto found = self->ctx().ieee802154.scan(
            chans, sizeof(chans) / sizeof(chans[0]),
            static_cast<uint32_t>(self->dwell_ms_));
        if (!found.ok()) {
            gallus::Log::warn(kTag, "survey failed: %s", found.message());
            return;
        }

        unsigned active = 0;
        for (size_t i = 0; i < found.value(); ++i) {
            const auto& c = chans[i];
            if (c.frames == 0 && c.pan_count == 0) {
                continue;
            }
            ++active;
            gallus::Log::info(kTag, "  ch%u  energy=%ddBm frames=%u pans=%u devs=%u",
                              static_cast<unsigned>(c.channel),
                              static_cast<int>(c.energy_dbm),
                              static_cast<unsigned>(c.frames),
                              static_cast<unsigned>(c.pan_count),
                              static_cast<unsigned>(c.device_count));
        }
        if (active == 0) {
            gallus::Log::info(kTag, "no 802.15.4 activity");
        }
    }

    gallus::JobHandle job_;
    int32_t period_ms_ = 0;
    int32_t dwell_ms_ = 120;
};

}  // namespace

GALLUS_MODULE(Ieee802154ScanModule, ieee802154_scan)
