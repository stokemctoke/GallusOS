/// GPIO Blink — demonstrates the GPIO reservation manager.
///
/// Requests its pin through GpioService (never touching the driver),
/// toggles it on a scheduled job, publishes GPIOChanged, and releases
/// the pin cleanly on stop.

#include "gallus/log.hpp"
#include "gallus/sdk/module.hpp"

namespace {

constexpr const char* kTag = "gpio_blink";

class GpioBlinkModule : public gallus::sdk::Module {
public:
    /// Payload published with GPIOChanged.
    struct GpioChangedPayload {
        uint8_t pin;
        uint8_t level;
    };

    gallus::Status start() override {
        // Default 27 = XIAO ESP32-C5 onboard user LED.
        pin_ = ctx().config.getInt("gpio_blink", "pin", 27);
        period_ms_ = ctx().config.getInt("gpio_blink", "period_ms", 1000);

        GALLUS_RETURN_IF_ERROR(ctx().gpio.requestPin(pin_, "gpio_blink"));

        gallus::Status status =
            ctx().gpio.configure(pin_, gallus::drivers::PinMode::Output);
        if (!status.ok()) {
            (void)ctx().gpio.releasePin(pin_, "gpio_blink");
            return status;
        }

        auto job = ctx().scheduler.every(static_cast<uint32_t>(period_ms_),
                                         &GpioBlinkModule::tick, this,
                                         gallus::Priority::Slow);
        if (!job.ok()) {
            (void)ctx().gpio.releasePin(pin_, "gpio_blink");
            return job.status();
        }
        job_ = job.value();

        gallus::Log::info(kTag, "blinking pin %d every %d ms",
                          static_cast<int>(pin_),
                          static_cast<int>(period_ms_));
        return gallus::Status::success();
    }

    gallus::Status stop() override {
        (void)ctx().scheduler.cancel(job_);
        job_ = {};
        (void)ctx().gpio.write(pin_, false);
        (void)ctx().gpio.releasePin(pin_, "gpio_blink");
        return gallus::Status::success();
    }

private:
    static void tick(void* arg) {
        auto* self = static_cast<GpioBlinkModule*>(arg);
        self->level_ = !self->level_;
        (void)self->ctx().gpio.write(self->pin_, self->level_);

        const GpioChangedPayload payload = {
            .pin = static_cast<uint8_t>(self->pin_),
            .level = static_cast<uint8_t>(self->level_ ? 1 : 0),
        };
        (void)self->ctx().events.publish(
            gallus::Event::make(gallus::EventId::GPIOChanged, payload));
    }

    gallus::JobHandle job_;
    int32_t pin_ = 27;
    int32_t period_ms_ = 1000;
    bool level_ = false;
};

}  // namespace

GALLUS_MODULE(GpioBlinkModule, gpio_blink)
