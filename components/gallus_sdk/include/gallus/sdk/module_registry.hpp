#pragma once

#include <cstddef>

#include "gallus/sdk/module.hpp"

/// @file module_registry.hpp
/// @brief Compile-time module registration.
///
/// Each module's generated manifest code instantiates a
/// ModuleRegistrar at static-init time; the registrars fill the
/// registry before app_main runs. Module components are linked with
/// WHOLE_ARCHIVE so the registrars survive the linker.

namespace gallus::sdk {

class ModuleRegistry {
public:
    static constexpr size_t kMaxModules = 16;

    struct Record {
        const ModuleInfo* info = nullptr;
        ModuleFactory factory = nullptr;
    };

    /// The process-wide registry (safe during static initialization).
    static ModuleRegistry& instance();

    /// Called by ModuleRegistrar only. Returns false when full.
    bool add(const ModuleInfo& info, ModuleFactory factory);

    [[nodiscard]] size_t count() const { return count_; }
    [[nodiscard]] const Record& at(size_t index) const {
        return records_[index];
    }

private:
    ModuleRegistry() = default;

    Record records_[kMaxModules] = {};
    size_t count_ = 0;
};

/// One static instance per module, emitted by the manifest generator.
class ModuleRegistrar {
public:
    ModuleRegistrar(const ModuleInfo& info, ModuleFactory factory) {
        (void)ModuleRegistry::instance().add(info, factory);
    }
};

}  // namespace gallus::sdk
