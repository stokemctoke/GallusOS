#include "gallus/kernel.hpp"

namespace gallus {

namespace {
constexpr const char* kTag = "Kernel";
}

Kernel& Kernel::instance() {
    static Kernel kernel;
    return kernel;
}

Status Kernel::init() {
    if (initialized_) {
        return Error::InvalidState;
    }

    Log::info(kTag, "GallusOS v%s — kernel init", version());

    Status status = events_.init();
    if (!status.ok()) {
        Log::error(kTag, "event bus init failed: %s", status.message());
        return status;
    }

    status = scheduler_.init();
    if (!status.ok()) {
        Log::error(kTag, "scheduler init failed: %s", status.message());
        return status;
    }

    initialized_ = true;

    status = events_.publish(Event::make(EventId::SystemBoot));
    if (!status.ok()) {
        return status;
    }

    Log::info(kTag, "kernel up (event bus + scheduler)");
    return Status::success();
}

Status Kernel::start() {
    if (!initialized_) {
        return Error::InvalidState;
    }
    Log::info(kTag, "system ready");
    return events_.publish(Event::make(EventId::SystemReady));
}

}  // namespace gallus
