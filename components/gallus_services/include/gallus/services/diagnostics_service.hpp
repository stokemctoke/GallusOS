#pragma once

struct cJSON;

#include "gallus/error.hpp"
#include "gallus/kernel.hpp"
#include "gallus/services/storage_service.hpp"

/// @file diagnostics_service.hpp
/// @brief Developer diagnostics snapshot for REST and the dashboard.
///
/// Aggregates heap, scheduler, event-bus, filesystem and task stats
/// into a single JSON document. Keeps policy out of the route handlers.

namespace gallus::services {

class DiagnosticsService {
public:
    DiagnosticsService(Kernel& kernel, StorageService& storage)
        : kernel_(kernel), storage_(storage) {}

    /// Build a diagnostics JSON object. Caller owns the returned cJSON*.
    cJSON* snapshotJson() const;

private:
    Kernel& kernel_;
    StorageService& storage_;
};

}  // namespace gallus::services
