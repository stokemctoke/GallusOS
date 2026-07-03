#include "gallus/sdk/module_manager.hpp"

#include <cstdio>
#include <cstring>

#include "gallus/log.hpp"

namespace gallus::sdk {

namespace {
constexpr const char* kTag = "Modules";
constexpr const char* kModulesNs = "modules";
}  // namespace

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

ModuleManager::Entry* ModuleManager::findEntry(const char* name) {
    if (name == nullptr) {
        return nullptr;
    }
    for (size_t i = 0; i < count_; ++i) {
        if (std::strcmp(entries_[i].info->name, name) == 0) {
            return &entries_[i];
        }
    }
    return nullptr;
}

const ModuleManager::Entry* ModuleManager::findEntry(const char* name) const {
    if (name == nullptr) {
        return nullptr;
    }
    for (size_t i = 0; i < count_; ++i) {
        if (std::strcmp(entries_[i].info->name, name) == 0) {
            return &entries_[i];
        }
    }
    return nullptr;
}

bool ModuleManager::isEnabled(const char* name) const {
    if (name == nullptr) {
        return false;
    }
    return ctx_.config.getBool(kModulesNs, name, true);
}

void ModuleManager::destroyInstance(Entry& entry) {
    if (entry.instance == nullptr) {
        return;
    }
    entry.instance->shutdown();
    delete entry.instance;
    entry.instance = nullptr;
}

Status ModuleManager::initOne(Entry& entry) {
    if (entry.factory == nullptr) {
        return Error::Internal;
    }

    destroyInstance(entry);

    entry.instance = entry.factory();
    if (entry.instance == nullptr) {
        entry.state = State::Failed;
        return Error::Internal;
    }

    const Status status = entry.instance->initialize(ctx_);
    if (!status.ok()) {
        destroyInstance(entry);
        entry.state = State::Failed;
        return status;
    }

    entry.state = State::Initialized;
    return Status::success();
}

Status ModuleManager::startOne(Entry& entry) {
    if (entry.state != State::Initialized &&
        entry.state != State::Stopped) {
        return Error::InvalidState;
    }
    if (entry.instance == nullptr) {
        return Error::InvalidState;
    }

    Status status = entry.instance->start();
    if (!status.ok()) {
        entry.state = State::Failed;
        return status;
    }

    status = entry.instance->registerRoutes(ctx_.rest);
    if (!status.ok()) {
        Log::warn(kTag, "%s — route registration failed: %s", entry.info->name,
                  status.message());
    }

    entry.state = State::Started;
    publishLifecycle(EventId::ModuleStarted, entry.info->name);
    Log::info(kTag, "%s — started", entry.info->name);
    return Status::success();
}

Status ModuleManager::stopOne(Entry& entry) {
    if (entry.state != State::Started) {
        return Error::InvalidState;
    }
    if (entry.instance == nullptr) {
        return Error::InvalidState;
    }

    const Status status = entry.instance->stop();
    if (!status.ok()) {
        entry.state = State::Failed;
        return status;
    }

    entry.state = State::Stopped;
    publishLifecycle(EventId::ModuleStopped, entry.info->name);
    Log::info(kTag, "%s — stopped", entry.info->name);
    return Status::success();
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
        entry.factory = record.factory;

        if (!isEnabled(record.info->name)) {
            entry.state = State::Disabled;
            Log::info(kTag, "%s v%s — disabled by config",
                      record.info->name, record.info->version);
            continue;
        }

        const Status status = initOne(entry);
        if (!status.ok()) {
            Log::error(kTag, "%s — initialize failed: %s", record.info->name,
                       status.message());
            continue;
        }

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
        const Status status = startOne(entry);
        if (!status.ok()) {
            Log::error(kTag, "%s — start failed: %s", entry.info->name,
                       status.message());
        }
    }
    return Status::success();
}

Status ModuleManager::stopAll() {
    for (size_t i = 0; i < count_; ++i) {
        Entry& entry = entries_[i];
        if (entry.state != State::Started) {
            continue;
        }
        const Status status = stopOne(entry);
        if (!status.ok()) {
            Log::error(kTag, "%s — stop failed: %s", entry.info->name,
                       status.message());
        }
    }
    return Status::success();
}

Status ModuleManager::start(const char* name) {
    Entry* entry = findEntry(name);
    if (entry == nullptr) {
        return Error::NotFound;
    }
    if (!isEnabled(name)) {
        return Error::InvalidState;
    }
    return startOne(*entry);
}

Status ModuleManager::stop(const char* name) {
    Entry* entry = findEntry(name);
    if (entry == nullptr) {
        return Error::NotFound;
    }
    return stopOne(*entry);
}

Status ModuleManager::enable(const char* name) {
    Entry* entry = findEntry(name);
    if (entry == nullptr) {
        return Error::NotFound;
    }

    const Status cfg = ctx_.config.setBool(kModulesNs, name, true);
    if (!cfg.ok()) {
        return cfg;
    }

    if (entry->state == State::Started || entry->state == State::Stopped ||
        entry->state == State::Initialized) {
        return Status::success();
    }

    const Status status = initOne(*entry);
    if (!status.ok()) {
        Log::error(kTag, "%s — enable init failed: %s", name, status.message());
        return status;
    }

    Log::info(kTag, "%s — enabled", name);
    return Status::success();
}

Status ModuleManager::disable(const char* name) {
    Entry* entry = findEntry(name);
    if (entry == nullptr) {
        return Error::NotFound;
    }

    if (entry->state == State::Started) {
        const Status stopped = stopOne(*entry);
        if (!stopped.ok()) {
            return stopped;
        }
    }

    destroyInstance(*entry);

    const Status cfg = ctx_.config.setBool(kModulesNs, name, false);
    if (!cfg.ok()) {
        return cfg;
    }

    entry->state = State::Disabled;
    Log::info(kTag, "%s — disabled", name);
    return Status::success();
}

void ModuleManager::publishLifecycle(EventId id, const char* name) {
    ModuleEventPayload payload = {};
    snprintf(payload.name, sizeof(payload.name), "%s", name);
    (void)ctx_.events.publish(Event::make(id, payload));
}

}  // namespace gallus::sdk
