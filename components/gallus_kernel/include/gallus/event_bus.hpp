#pragma once

#include <atomic>
#include <cstdint>

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include "gallus/error.hpp"
#include "gallus/event.hpp"

/// @file event_bus.hpp
/// @brief Kernel publish/subscribe event bus.
///
/// Delivery model:
///  - publish() copies the event into a bounded FreeRTOS queue and
///    never blocks. When the queue is full the OLDEST event is dropped
///    and a diagnostic counter is incremented (memory policy: bounded
///    queues, drop-oldest, never block the publisher).
///  - A single dispatcher task drains the queue and invokes handlers
///    synchronously. Handlers must be quick; offload heavy work to the
///    Scheduler.
///  - Subscription storage is static (kMaxSubscriptions), allocated at
///    init — no runtime heap churn.

namespace gallus {

/// Identifies a subscription for later unsubscribe(). Obtained from
/// EventBus::subscribe().
struct SubscriptionHandle {
    uint8_t slot = 0xFF;
    uint16_t generation = 0;

    [[nodiscard]] bool valid() const { return slot != 0xFF; }
};

class EventBus {
public:
    /// Event handler. @p ctx is the pointer given at subscribe time.
    using Handler = void (*)(const Event& event, void* ctx);

    static constexpr size_t kMaxSubscriptions = 32;
    static constexpr size_t kDefaultQueueDepth = 32;

    EventBus() = default;
    EventBus(const EventBus&) = delete;
    EventBus& operator=(const EventBus&) = delete;

    /// Create the queue and start the dispatcher task.
    Status init(size_t queue_depth = kDefaultQueueDepth);

    /// Queue @p event for delivery. Never blocks; drops the oldest
    /// queued event when full.
    Status publish(const Event& event);

    /// ISR-safe variant of publish(). Drops the NEW event when the
    /// queue is full (draining from an ISR is not safe).
    Status publishFromIsr(const Event& event,
                          BaseType_t* higher_prio_task_woken);

    /// Invoke @p handler on the dispatcher task for every event with
    /// @p id. @p ctx is passed through untouched.
    Result<SubscriptionHandle> subscribe(EventId id, Handler handler,
                                         void* ctx = nullptr);

    /// Remove a previous subscription. Safe to call with a stale
    /// handle (returns NotFound).
    Status unsubscribe(SubscriptionHandle handle);

    /// Events dropped due to queue overflow since init.
    [[nodiscard]] uint32_t droppedCount() const {
        return dropped_.load(std::memory_order_relaxed);
    }

    /// Events delivered to handlers since init.
    [[nodiscard]] uint32_t deliveredCount() const {
        return delivered_.load(std::memory_order_relaxed);
    }

private:
    struct Subscription {
        EventId id = EventId::SystemBoot;
        Handler handler = nullptr;
        void* ctx = nullptr;
        uint16_t generation = 0;
        bool active = false;
    };

    static void dispatcherTaskEntry(void* self);
    void dispatchLoop();
    void deliver(const Event& event);

    Subscription subs_[kMaxSubscriptions] = {};
    QueueHandle_t queue_ = nullptr;
    SemaphoreHandle_t subs_mutex_ = nullptr;
    TaskHandle_t dispatcher_ = nullptr;
    std::atomic<uint32_t> dropped_{0};
    std::atomic<uint32_t> delivered_{0};
    bool initialized_ = false;
};

}  // namespace gallus
