#pragma once

#include <cstddef>
#include <cstdint>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#include "gallus/error.hpp"
#include "gallus/event_bus.hpp"
#include "gallus/services/storage_service.hpp"

/// @file config_service.hpp
/// @brief Hierarchical persistent configuration.
///
/// Configuration lives on LittleFS as one JSON file per namespace:
/// /fs/config/<namespace>.json (e.g. "system", "wifi", "gpio", or a
/// module name). Reads never fail — a missing key yields the caller's
/// default. Writes persist immediately and publish ConfigChanged.
///
/// Files are loaded, modified and released per operation; config
/// access is rare, so this deliberately trades a transient parse for
/// zero steady-state RAM cost.

namespace gallus::services {

class ConfigService {
public:
    /// Payload carried by EventId::ConfigChanged.
    struct ChangedEvent {
        char ns[16];
        char key[16];
    };

    static constexpr size_t kMaxFileBytes = 4096;

    ConfigService(StorageService& storage, EventBus& events)
        : storage_(storage), events_(events) {}
    ConfigService(const ConfigService&) = delete;
    ConfigService& operator=(const ConfigService&) = delete;

    /// Create the /config directory. Requires a mounted StorageService.
    Status init();

    // Reads — return the default on any miss or error.
    int32_t getInt(const char* ns, const char* key, int32_t def) const;
    bool getBool(const char* ns, const char* key, bool def) const;
    float getFloat(const char* ns, const char* key, float def) const;

    /// Copy the string value (or @p def) into @p out (NUL-terminated).
    Status getString(const char* ns, const char* key, char* out, size_t cap,
                     const char* def) const;

    // Writes — persist immediately, publish ConfigChanged on success.
    Status setInt(const char* ns, const char* key, int32_t value);
    Status setBool(const char* ns, const char* key, bool value);
    Status setFloat(const char* ns, const char* key, float value);
    Status setString(const char* ns, const char* key, const char* value);

    /// Remove one key from a namespace.
    Status erase(const char* ns, const char* key);

    /// Delete all persisted namespace files (factory reset).
    Status resetAll();

    /// Export a namespace as JSON. Sensitive keys are redacted when
    /// @p redact is true. Caller owns the returned cJSON*.
    void* exportNamespace(const char* ns, bool redact) const;

private:
    void* loadNamespace(const char* ns) const;  // returns cJSON*
    Status saveNamespace(const char* ns, void* doc);
    Status setNumber(const char* ns, const char* key, double value);
    void publishChanged(const char* ns, const char* key);
    void path(const char* ns, char* out, size_t cap) const;

    StorageService& storage_;
    EventBus& events_;
    mutable SemaphoreHandle_t mutex_ = nullptr;
    bool initialized_ = false;
};

}  // namespace gallus::services
