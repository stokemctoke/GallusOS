/// Hello World — the smallest possible GallusOS module.
///
/// Demonstrates: reading its own config namespace, scheduling a
/// periodic job, clean start/stop, and zero direct hardware access.

#include "esp_timer.h"

#include "gallus/log.hpp"
#include "gallus/sdk/module.hpp"

namespace {

constexpr const char* kTag = "hello_world";

class HelloWorldModule : public gallus::sdk::Module {
public:
    gallus::Status start() override {
        char message[64];
        (void)ctx().config.getString("hello_world", "message", message,
                                     sizeof(message),
                                     "Hello from GallusOS!");
        gallus::Log::info(kTag, "%s", message);

        const int32_t period_s =
            ctx().config.getInt("hello_world", "period_s", 60);
        auto job = ctx().scheduler.every(
            static_cast<uint32_t>(period_s) * 1000, &HelloWorldModule::tick,
            this, gallus::Priority::Background);
        if (!job.ok()) {
            return job.status();
        }
        job_ = job.value();
        return gallus::Status::success();
    }

    gallus::Status stop() override {
        (void)ctx().scheduler.cancel(job_);
        job_ = {};
        return gallus::Status::success();
    }

private:
    static void tick(void* self) {
        (void)self;
        gallus::Log::info(kTag, "still here — uptime %llu s",
                          static_cast<unsigned long long>(
                              esp_timer_get_time() / 1000000));
    }

    gallus::JobHandle job_;
};

}  // namespace

GALLUS_MODULE(HelloWorldModule, hello_world)
