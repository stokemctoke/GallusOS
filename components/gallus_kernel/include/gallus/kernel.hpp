#pragma once

#include "gallus/error.hpp"
#include "gallus/event_bus.hpp"
#include "gallus/log.hpp"
#include "gallus/scheduler.hpp"
#include "gallus/version.hpp"

/// @file kernel.hpp
/// @brief The GallusOS kernel: owns the cross-cutting primitives
/// (event bus, scheduler, logging) and the boot sequence.
///
/// Kernel::instance() is the single sanctioned global access point
/// (see LANGUAGE AND ERROR HANDLING in the spec). Everything else is
/// reached through it or injected explicitly.

namespace gallus {

class Kernel {
public:
    /// The kernel singleton — the one sanctioned global.
    static Kernel& instance();

    Kernel(const Kernel&) = delete;
    Kernel& operator=(const Kernel&) = delete;

    /// Bring up logging, the event bus and the scheduler, then publish
    /// SystemBoot. Call once from app_main before anything else.
    Status init();

    /// Publish SystemReady. Call after services/modules are up
    /// (Phase 2+ will move service startup in between init and start).
    Status start();

    EventBus& events() { return events_; }
    Scheduler& scheduler() { return scheduler_; }

    /// Firmware version string.
    static constexpr const char* version() { return kVersion; }

private:
    Kernel() = default;

    EventBus events_;
    Scheduler scheduler_;
    bool initialized_ = false;
};

}  // namespace gallus
