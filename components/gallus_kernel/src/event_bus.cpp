#include "gallus/event_bus.hpp"

#include "gallus/log.hpp"

namespace gallus {

namespace {

constexpr const char* kTag = "EventBus";
constexpr uint32_t kDispatcherStackBytes = 4096;
constexpr UBaseType_t kDispatcherPriority = 8;

}  // namespace

const char* toString(EventId id) {
    switch (id) {
        case EventId::SystemBoot:         return "SystemBoot";
        case EventId::SystemReady:        return "SystemReady";
        case EventId::ConfigChanged:      return "ConfigChanged";
        case EventId::BatteryChanged:     return "BatteryChanged";
        case EventId::GPIOChanged:        return "GPIOChanged";
        case EventId::WiFiConnected:      return "WiFiConnected";
        case EventId::WiFiDisconnected:   return "WiFiDisconnected";
        case EventId::ModuleStarted:      return "ModuleStarted";
        case EventId::ModuleStopped:      return "ModuleStopped";
        case EventId::ButtonPressed:      return "ButtonPressed";
        case EventId::OTAStarted:         return "OTAStarted";
        case EventId::OTAProgress:        return "OTAProgress";
        case EventId::OTAFinished:        return "OTAFinished";
        case EventId::ClientConnected:    return "ClientConnected";
        case EventId::ClientDisconnected: return "ClientDisconnected";
        case EventId::DisplayUpdated:     return "DisplayUpdated";
        case EventId::TimeSynced:         return "TimeSynced";
        case EventId::Heartbeat:          return "Heartbeat";
        case EventId::PowerModeChanged:   return "PowerModeChanged";
        default:                          return "User";
    }
}

Status EventBus::init(size_t queue_depth) {
    if (initialized_) {
        return Error::InvalidState;
    }
    if (queue_depth == 0) {
        return Error::InvalidArg;
    }

    queue_ = xQueueCreate(queue_depth, sizeof(Event));
    if (queue_ == nullptr) {
        return Error::NoMemory;
    }

    subs_mutex_ = xSemaphoreCreateMutex();
    if (subs_mutex_ == nullptr) {
        vQueueDelete(queue_);
        queue_ = nullptr;
        return Error::NoMemory;
    }

    const BaseType_t created =
        xTaskCreate(&EventBus::dispatcherTaskEntry, "gallus_events",
                    kDispatcherStackBytes, this, kDispatcherPriority,
                    &dispatcher_);
    if (created != pdPASS) {
        vSemaphoreDelete(subs_mutex_);
        vQueueDelete(queue_);
        subs_mutex_ = nullptr;
        queue_ = nullptr;
        return Error::NoMemory;
    }

    initialized_ = true;
    return Status::success();
}

Status EventBus::publish(const Event& event) {
    if (!initialized_) {
        return Error::InvalidState;
    }

    if (xQueueSend(queue_, &event, 0) == pdTRUE) {
        return Status::success();
    }

    // Queue full: drop the oldest event to make room. The publisher is
    // never blocked (memory policy).
    Event discarded;
    (void)xQueueReceive(queue_, &discarded, 0);
    dropped_.fetch_add(1, std::memory_order_relaxed);

    if (xQueueSend(queue_, &event, 0) == pdTRUE) {
        return Status::success();
    }
    dropped_.fetch_add(1, std::memory_order_relaxed);
    return Error::QueueFull;
}

Status EventBus::publishFromIsr(const Event& event,
                                BaseType_t* higher_prio_task_woken) {
    if (!initialized_) {
        return Error::InvalidState;
    }
    if (xQueueSendFromISR(queue_, &event, higher_prio_task_woken) != pdTRUE) {
        dropped_.fetch_add(1, std::memory_order_relaxed);
        return Error::QueueFull;
    }
    return Status::success();
}

Result<SubscriptionHandle> EventBus::subscribe(EventId id, Handler handler,
                                               void* ctx) {
    if (!initialized_) {
        return Error::InvalidState;
    }
    if (handler == nullptr) {
        return Error::InvalidArg;
    }

    xSemaphoreTake(subs_mutex_, portMAX_DELAY);
    for (size_t i = 0; i < kMaxSubscriptions; ++i) {
        Subscription& sub = subs_[i];
        if (sub.active) {
            continue;
        }
        sub.id = id;
        sub.handler = handler;
        sub.ctx = ctx;
        sub.generation++;
        sub.active = true;
        xSemaphoreGive(subs_mutex_);

        SubscriptionHandle handle;
        handle.slot = static_cast<uint8_t>(i);
        handle.generation = sub.generation;
        return handle;
    }
    xSemaphoreGive(subs_mutex_);

    Log::error(kTag, "subscription table full (%u slots)",
               static_cast<unsigned>(kMaxSubscriptions));
    return Error::NoMemory;
}

Status EventBus::unsubscribe(SubscriptionHandle handle) {
    if (!initialized_) {
        return Error::InvalidState;
    }
    if (!handle.valid() || handle.slot >= kMaxSubscriptions) {
        return Error::InvalidArg;
    }

    xSemaphoreTake(subs_mutex_, portMAX_DELAY);
    Subscription& sub = subs_[handle.slot];
    if (!sub.active || sub.generation != handle.generation) {
        xSemaphoreGive(subs_mutex_);
        return Error::NotFound;
    }
    sub.active = false;
    sub.handler = nullptr;
    sub.ctx = nullptr;
    xSemaphoreGive(subs_mutex_);
    return Status::success();
}

void EventBus::dispatcherTaskEntry(void* self) {
    static_cast<EventBus*>(self)->dispatchLoop();
}

void EventBus::dispatchLoop() {
    Event event;
    for (;;) {
        if (xQueueReceive(queue_, &event, portMAX_DELAY) == pdTRUE) {
            deliver(event);
        }
    }
}

void EventBus::deliver(const Event& event) {
    // Snapshot matching handlers under the lock, invoke outside it so a
    // handler can subscribe/unsubscribe without deadlocking.
    Handler handlers[kMaxSubscriptions];
    void* contexts[kMaxSubscriptions];
    size_t count = 0;

    xSemaphoreTake(subs_mutex_, portMAX_DELAY);
    for (size_t i = 0; i < kMaxSubscriptions; ++i) {
        const Subscription& sub = subs_[i];
        if (sub.active && sub.id == event.id) {
            handlers[count] = sub.handler;
            contexts[count] = sub.ctx;
            ++count;
        }
    }
    xSemaphoreGive(subs_mutex_);

    for (size_t i = 0; i < count; ++i) {
        handlers[i](event, contexts[i]);
    }
    delivered_.fetch_add(1, std::memory_order_relaxed);
}

}  // namespace gallus
