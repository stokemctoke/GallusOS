/// I2C Scanner — probes the bus and logs found 7-bit addresses.

#include <cstdio>

#include "gallus/log.hpp"
#include "gallus/sdk/module.hpp"

namespace {

constexpr const char* kTag = "i2c_scanner";

class I2cScannerModule : public gallus::sdk::Module {
public:
    gallus::Status start() override {
        period_ms_ =
            ctx().config.getInt("i2c_scanner", "period_ms", 30000);

        auto job = ctx().scheduler.every(
            static_cast<uint32_t>(period_ms_), &I2cScannerModule::tick, this,
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
        auto* self = static_cast<I2cScannerModule*>(arg);
        if (!self->ctx().i2c.ready()) {
            gallus::Log::warn(kTag, "I2C bus not ready");
            return;
        }

        uint8_t addrs[16];
        const auto found = self->ctx().i2c.scan(addrs, sizeof(addrs));
        if (!found.ok()) {
            gallus::Log::error(kTag, "scan failed: %s", found.message());
            return;
        }

        if (found.value() == 0) {
            gallus::Log::info(kTag, "no I2C devices found");
            return;
        }

        char line[96] = {};
        int offset = snprintf(line, sizeof(line), "found %u device(s):",
                              static_cast<unsigned>(found.value()));
        for (size_t i = 0; i < found.value() && offset > 0; ++i) {
            offset += snprintf(line + offset, sizeof(line) - offset, " 0x%02X",
                               addrs[i]);
        }
        gallus::Log::info(kTag, "%s", line);
    }

    gallus::JobHandle job_;
    int32_t period_ms_ = 30000;
};

}  // namespace

GALLUS_MODULE(I2cScannerModule, i2c_scanner)
