#include "gallus/sdk/module_manager.hpp"

#include <cstdio>

#include "gallus/log.hpp"

namespace gallus::sdk {

namespace {
constexpr const char* kTag = "Modules";
}

const char* ModuleManager::stateName(State state) {
    switch (state) {
        case State::Registered:  return "registered";
        case State::Disabled:    return "disabled";
        case State::Initialized: return "initialized";
        case State::Started:     return "started";
        case State::Stopped:     return "stopped";
        case State::Failed:      return "failed";
    }
    return "unknown";
}

Status ModuleManager::initAll() {
    if (count_ != 0) {
        return Error::InvalidState;
    }

    const ModuleRegistry& registry = ModuleRegistry::instance();
    Log::info(kTag, "%u module(s) registered",
              static_cast<unsigned>(registry.count()));

    for (size_t i = 0; i < registry.count(); ++i) {
        const ModuleRegistry::Record& record = registry.at(i);
        Entry& entry = entries_[count_++];
        entry.info = record.info;

        if (!ctx_.config.getBool("modules", record.info->name, true)) {
            entry.state = State::Disabled;
            Log::info(kTag, "%s v%s — disabled by config",
                      record.info->name, record.info->version);
            continue;
        }

        entry.instance = record.factory();
        if (entry.instance == nullptr) {
            entry.state = State::Failed;
            Log::error(kTag, "%s — factory returned nothing",
                       record.info->name);
            continue;
        }

        const Status status = entry.instance->initialize(ctx_);
        if (!status.ok()) {
            entry.state = State::Failed;
            Log::error(kTag, "%s — initialize failed: %s",
                       record.info->name, status.message());
            continue;
        }

        entry.state = State::Initialized;
        Log::info(kTag, "%s v%s (%s) — initialized", record.info->name,
                  record.info->version, record.info->category);
    }
    return Status::success();
}

Status ModuleManager::startAll() {
    for (size_t i = 0; i < count_; ++i) {
        Entry& entry = entries_[i];
        if (entry.state != State::Initialized &&
            entry.state != State::Stopped) {
            continue;
        }

        Status status = entry.instance->start();
        if (!status.ok()) {
            entry.state = State::Failed;
            Log::error(kTag, "%s — start failed: %s", entry.info->name,
                       status.message());
            continue;
        }

        status = entry.instance->registerRoutes(ctx_.rest);
        if (!status.ok()) {
            Log::warn(kTag, "%s — route registration failed: %s",
                      entry.info->name, status.message());
        }

        entry.state = State::Started;
        publishLifecycle(EventId::ModuleStarted, entry.info->name);
        Log::info(kTag, "%s — started", entry.info->name);
    }
    return Status::success();
}

Status ModuleManager::stopAll() {
    for (size_t i = 0; i < count_; ++i) {
        Entry& entry = entries_[i];
        if (entry.state != State::Started) {
            continue;
        }

        const Status status = entry.instance->stop();
        if (!status.ok()) {
            entry.state = State::Failed;
            Log::error(kTag, "%s — stop failed: %s", entry.info->name,
                       status.message());
            continue;
        }

        entry.state = State::Stopped;
        publishLifecycle(EventId::ModuleStopped, entry.info->name);
        Log::info(kTag, "%s — stopped", entry.info->name);
    }
    return Status::success();
}

void ModuleManager::publishLifecycle(EventId id, const char* name) {
    ModuleEventPayload payload = {};
    snprintf(payload.name, sizeof(payload.name), "%s", name);
    (void)ctx_.events.publish(Event::make(id, payload));
}

}  // namespace gallus::sdk
