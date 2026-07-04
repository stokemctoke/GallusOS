#pragma once

#include <cstddef>
#include <cstdint>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#include "gallus/drivers/gpio.hpp"
#include "gallus/error.hpp"
#include "gallus/event_bus.hpp"

/// @file gpio_service.hpp
/// @brief GPIO reservation manager and pin access policy.
///
/// The only sanctioned path to GPIO for anything above the driver
/// layer. Pins must be allocated (requestPin/reserve/claim) before
/// they can be configured, read or written.
///
/// Pin states:
///  - Free:      available to anyone via requestPin().
///  - Allocated: owned; released with releasePin().
///  - Reserved:  owned by a system service; a module can take it only
///               with claim(force=true) (user override).
///  - Critical:  protected board pins (battery circuit, boot button);
///               never claimable, set from HAL board facts at init().

namespace gallus::services {

class GpioService {
public:
    static constexpr size_t kOwnerLen = 16;

    enum class PinState : uint8_t {
        Invalid,   ///< Not a usable pin on this board.
        Free,
        Allocated,
        Reserved,
        Critical,
    };

    /// Snapshot row for diagnostics / the future dashboard.
    struct PinInfo {
        int8_t pin;
        PinState state;
        char owner[kOwnerLen];
    };

    explicit GpioService(EventBus& events) : events_(events) {}
    GpioService(const GpioService&) = delete;
    GpioService& operator=(const GpioService&) = delete;

    /// Mark the board's critical pins. Call once at boot.
    Status init();

    /// Allocate a Free pin to @p owner.
    Status requestPin(int pin, const char* owner);

    /// Reserve a pin for a system service (stronger than Allocated:
    /// modules need a forced claim to take it).
    Status reserve(int pin, const char* owner);

    /// Take a pin, optionally overriding a Reserved pin
    /// (@p force = user override). Critical pins always refuse.
    Status claim(int pin, const char* owner, bool force = false);

    /// Release a pin held by @p owner and reset it to power-on state.
    /// Fails with PermissionDenied when @p owner does not match the
    /// holder recorded at allocation time.
    Status releasePin(int pin, const char* owner);

    // Pin access — allowed only on allocated/reserved pins.
    Status configure(int pin, drivers::PinMode mode);
    Status write(int pin, bool level);
    Result<bool> read(int pin);

    /// Fill @p out with up to @p cap rows describing every valid pin.
    /// @return rows written.
    size_t snapshot(PinInfo* out, size_t cap) const;

private:
    struct Entry {
        PinState state = PinState::Invalid;
        char owner[kOwnerLen] = {};
    };

    Status allocate(int pin, const char* owner, PinState target, bool force);
    [[nodiscard]] bool accessible(int pin) const;

    Entry entries_[32] = {};  // indexed by GPIO number
    EventBus& events_;
    mutable SemaphoreHandle_t mutex_ = nullptr;
    bool initialized_ = false;
};

}  // namespace gallus::services
