#include "gallus/sdk/module_registry.hpp"

namespace gallus::sdk {

ModuleRegistry& ModuleRegistry::instance() {
    static ModuleRegistry registry;
    return registry;
}

bool ModuleRegistry::add(const ModuleInfo& info, ModuleFactory factory) {
    if (count_ >= kMaxModules || factory == nullptr) {
        return false;
    }
    records_[count_].info = &info;
    records_[count_].factory = factory;
    ++count_;
    return true;
}

}  // namespace gallus::sdk
