#pragma once

#include <cstdint>
#include <cstring>
#include <type_traits>

/// @file event.hpp
/// @brief Typed events carried by the kernel event bus.
///
/// An Event pairs a compile-time EventId with a small, trivially
/// copyable payload. Events are passed by value through a bounded
/// FreeRTOS queue, so payloads must fit Event::kMaxPayload bytes —
/// carry handles or indices, not buffers.

namespace gallus {

/// Well-known framework events. Modules define their own IDs starting
/// at EventId::UserBase.
enum class EventId : uint16_t {
    SystemBoot = 0,
    SystemReady,
    ConfigChanged,
    BatteryChanged,
    GPIOChanged,
    WiFiConnected,
    WiFiDisconnected,
    ModuleStarted,
    ModuleStopped,
    ButtonPressed,
    OTAStarted,
    OTAProgress,
    OTAFinished,
    ClientConnected,
    ClientDisconnected,
    DisplayUpdated,
    TimeSynced,
    Heartbeat,

    /// First ID available for module-defined events.
    UserBase = 0x1000,
};

/// @return Human-readable name for framework events ("User" otherwise).
const char* toString(EventId id);

/// A typed event with an optional inline payload.
struct Event {
    static constexpr size_t kMaxPayload = 32;

    EventId id = EventId::SystemBoot;
    uint8_t size = 0;
    uint8_t payload[kMaxPayload] = {};

    /// Build a payload-less event.
    static Event make(EventId id) {
        Event e;
        e.id = id;
        return e;
    }

    /// Build an event carrying @p data.
    template <typename T>
    static Event make(EventId id, const T& data) {
        static_assert(std::is_trivially_copyable_v<T>,
                      "event payloads must be trivially copyable");
        static_assert(sizeof(T) <= kMaxPayload,
                      "event payload exceeds Event::kMaxPayload");
        Event e;
        e.id = id;
        e.size = sizeof(T);
        std::memcpy(e.payload, &data, sizeof(T));
        return e;
    }

    /// Reinterpret the payload as T. Returns nullptr when the payload
    /// size does not match (wrong type or payload-less event).
    template <typename T>
    const T* as() const {
        static_assert(std::is_trivially_copyable_v<T>,
                      "event payloads must be trivially copyable");
        if (size != sizeof(T)) {
            return nullptr;
        }
        return reinterpret_cast<const T*>(payload);
    }
};

}  // namespace gallus
