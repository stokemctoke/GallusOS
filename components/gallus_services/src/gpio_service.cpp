#include "gallus/services/gpio_service.hpp"

#include <cstdio>
#include <cstring>

#include "gallus/hal/board.hpp"
#include "gallus/log.hpp"

namespace gallus::services {

namespace {
constexpr const char* kTag = "GPIO";
}

Status GpioService::init() {
    if (initialized_) {
        return Error::InvalidState;
    }

    mutex_ = xSemaphoreCreateMutex();
    if (mutex_ == nullptr) {
        return Error::NoMemory;
    }

    for (int pin : hal::board::kValidGpios) {
        entries_[pin].state = PinState::Free;
    }
    for (int pin : hal::board::kCriticalPins) {
        entries_[pin].state = PinState::Critical;
        snprintf(entries_[pin].owner, kOwnerLen, "system");
    }

    initialized_ = true;
    Log::info(kTag, "reservation manager up, %u critical pins protected",
              static_cast<unsigned>(sizeof(hal::board::kCriticalPins) /
                                    sizeof(hal::board::kCriticalPins[0])));
    return Status::success();
}

Status GpioService::allocate(int pin, const char* owner, PinState target,
                             bool force) {
    if (!initialized_) {
        return Error::InvalidState;
    }
    if (owner == nullptr || pin < 0 ||
        pin >= static_cast<int>(sizeof(entries_) / sizeof(entries_[0]))) {
        return Error::InvalidArg;
    }

    xSemaphoreTake(mutex_, portMAX_DELAY);
    Entry& entry = entries_[pin];

    Status status = Status::success();
    switch (entry.state) {
        case PinState::Invalid:
            status = Error::InvalidArg;
            break;
        case PinState::Critical:
            status = Error::PermissionDenied;
            break;
        case PinState::Allocated:
            status = Error::Busy;
            break;
        case PinState::Reserved:
            // Only a forced claim (user override) may take a reserved pin.
            status = force ? Status::success()
                           : Status::failure(Error::PermissionDenied);
            break;
        case PinState::Free:
            break;
    }

    if (status.ok()) {
        if (entry.state == PinState::Reserved) {
            Log::warn(kTag, "pin %d reserved by '%s' overridden by '%s'", pin,
                      entry.owner, owner);
        }
        entry.state = target;
        snprintf(entry.owner, kOwnerLen, "%s", owner);
        Log::debug(kTag, "pin %d -> '%s'", pin, owner);
    } else {
        Log::warn(kTag, "pin %d denied for '%s': %s (held by '%s')", pin,
                  owner, status.message(), entry.owner);
    }

    xSemaphoreGive(mutex_);
    return status;
}

Status GpioService::requestPin(int pin, const char* owner) {
    return allocate(pin, owner, PinState::Allocated, /*force=*/false);
}

Status GpioService::reserve(int pin, const char* owner) {
    return allocate(pin, owner, PinState::Reserved, /*force=*/false);
}

Status GpioService::claim(int pin, const char* owner, bool force) {
    return allocate(pin, owner, PinState::Allocated, force);
}

Status GpioService::releasePin(int pin, const char* owner) {
    if (!initialized_) {
        return Error::InvalidState;
    }
    if (owner == nullptr || pin < 0 ||
        pin >= static_cast<int>(sizeof(entries_) / sizeof(entries_[0]))) {
        return Error::InvalidArg;
    }

    xSemaphoreTake(mutex_, portMAX_DELAY);
    Entry& entry = entries_[pin];

    if (entry.state != PinState::Allocated &&
        entry.state != PinState::Reserved) {
        xSemaphoreGive(mutex_);
        return Error::NotFound;
    }

    // Only the holder may release (owner is stored truncated to
    // kOwnerLen, so compare what was actually stored).
    if (strncmp(entry.owner, owner, kOwnerLen - 1) != 0) {
        xSemaphoreGive(mutex_);
        Log::warn(kTag, "pin %d release denied for '%s' (held by '%s')",
                  pin, owner, entry.owner);
        return Error::PermissionDenied;
    }

    entry.state = PinState::Free;
    entry.owner[0] = '\0';
    xSemaphoreGive(mutex_);

    (void)drivers::GpioDriver::reset(pin);
    Log::debug(kTag, "pin %d released by '%s'", pin, owner);
    return Status::success();
}

bool GpioService::accessible(int pin) const {
    if (pin < 0 ||
        pin >= static_cast<int>(sizeof(entries_) / sizeof(entries_[0]))) {
        return false;
    }
    const PinState state = entries_[pin].state;
    return state == PinState::Allocated || state == PinState::Reserved;
}

Status GpioService::configure(int pin, drivers::PinMode mode) {
    if (!initialized_) {
        return Error::InvalidState;
    }
    if (!accessible(pin)) {
        return Error::PermissionDenied;
    }
    return drivers::GpioDriver::configure(pin, mode);
}

Status GpioService::write(int pin, bool level) {
    if (!initialized_) {
        return Error::InvalidState;
    }
    if (!accessible(pin)) {
        return Error::PermissionDenied;
    }
    return drivers::GpioDriver::write(pin, level);
}

Result<bool> GpioService::read(int pin) {
    if (!initialized_) {
        return Error::InvalidState;
    }
    if (!accessible(pin)) {
        return Error::PermissionDenied;
    }
    return drivers::GpioDriver::read(pin);
}

size_t GpioService::snapshot(PinInfo* out, size_t cap) const {
    if (!initialized_ || out == nullptr) {
        return 0;
    }

    size_t count = 0;
    xSemaphoreTake(mutex_, portMAX_DELAY);
    for (int pin : hal::board::kValidGpios) {
        if (count >= cap) {
            break;
        }
        out[count].pin = static_cast<int8_t>(pin);
        out[count].state = entries_[pin].state;
        snprintf(out[count].owner, kOwnerLen, "%s", entries_[pin].owner);
        ++count;
    }
    xSemaphoreGive(mutex_);
    return count;
}

}  // namespace gallus::services
