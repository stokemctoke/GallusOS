#pragma once

#include <cstdint>

#include "gallus/sdk/module.hpp"
#include "gallus/sdk/module_registry.hpp"

/// @file module_manager.hpp
/// @brief Module lifecycle management.
///
/// Instantiates every registered module (unless disabled via config
/// namespace "modules", key <name> = false), drives
/// initialize/start/stop, registers module REST routes, and publishes
/// ModuleStarted / ModuleStopped events.

namespace gallus::sdk {

class ModuleManager {
public:
    enum class State : uint8_t {
        Registered,   ///< Known, not yet instantiated.
        Disabled,     ///< Turned off via config.
        Initialized,  ///< initialize() succeeded.
        Started,      ///< start() succeeded — running.
        Stopped,      ///< stop() called.
        Failed,       ///< A lifecycle call failed.
    };

    /// Payload carried by ModuleStarted / ModuleStopped events.
    struct ModuleEventPayload {
        char name[24];
    };

    struct Entry {
        const ModuleInfo* info = nullptr;
        ModuleFactory factory = nullptr;
        Module* instance = nullptr;
        State state = State::Registered;
    };

    explicit ModuleManager(ModuleContext& ctx) : ctx_(ctx) {}
    ModuleManager(const ModuleManager&) = delete;
    ModuleManager& operator=(const ModuleManager&) = delete;

    /// Instantiate and initialize every enabled registered module.
    Status initAll();

    /// Start every initialized module and register its routes.
    Status startAll();

    /// Stop every started module.
    Status stopAll();

    /// Start one module by name (must be initialized or stopped).
    Status start(const char* name);

    /// Stop one running module by name.
    Status stop(const char* name);

    /// Persist enable flag, initialize if needed (does not auto-start).
    Status enable(const char* name);

    /// Stop, tear down, and persist disable flag for one module.
    Status disable(const char* name);

    [[nodiscard]] bool isEnabled(const char* name) const;

    [[nodiscard]] size_t count() const { return count_; }
    [[nodiscard]] const Entry& at(size_t index) const {
        return entries_[index];
    }

    static const char* stateName(State state);

private:
    Entry* findEntry(const char* name);
    const Entry* findEntry(const char* name) const;
    Status startOne(Entry& entry);
    Status stopOne(Entry& entry);
    Status initOne(Entry& entry);
    void destroyInstance(Entry& entry);
    void publishLifecycle(EventId id, const char* name);

    Entry entries_[ModuleRegistry::kMaxModules] = {};
    size_t count_ = 0;
    ModuleContext& ctx_;
};

}  // namespace gallus::sdk
