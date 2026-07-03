/// System Information — logs framework diagnostics on a schedule.

#include "esp_system.h"

#include "gallus/kernel.hpp"
#include "gallus/log.hpp"
#include "gallus/sdk/module.hpp"

namespace {

constexpr const char* kTag = "system_info";

class SystemInfoModule : public gallus::sdk::Module {
public:
    gallus::Status start() override {
        period_ms_ =
            ctx().config.getInt("system_info", "period_ms", 60000);

        if (period_ms_ <= 0) {
            gallus::Log::info(kTag, "ready — on-demand (period_ms=0)");
            return gallus::Status::success();
        }

        auto job = ctx().scheduler.every(
            static_cast<uint32_t>(period_ms_), &SystemInfoModule::tick, this,
            gallus::Priority::Background);
        if (!job.ok()) {
            return job.status();
        }
        job_ = job.value();
        tick(this);
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
        auto* self = static_cast<SystemInfoModule*>(arg);
        gallus::Kernel& kernel = gallus::Kernel::instance();
        gallus::Log::info(
            kTag,
            "heap free=%u min=%u | events delivered=%lu dropped=%lu | "
            "scheduler jobs=%u",
            static_cast<unsigned>(esp_get_free_heap_size()),
            static_cast<unsigned>(esp_get_minimum_free_heap_size()),
            static_cast<unsigned long>(kernel.events().deliveredCount()),
            static_cast<unsigned long>(kernel.events().droppedCount()),
            static_cast<unsigned>(kernel.scheduler().activeJobs()));
        (void)self;
    }

    gallus::JobHandle job_;
    int32_t period_ms_ = 60000;
};

}  // namespace

GALLUS_MODULE(SystemInfoModule, system_info)
