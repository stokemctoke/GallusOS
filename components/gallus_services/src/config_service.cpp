#include "gallus/services/config_service.hpp"

#include <cstdio>
#include <cstring>
#include <memory>

#include "cJSON.h"

#include "gallus/log.hpp"

namespace gallus::services {

namespace {
constexpr const char* kTag = "Config";
constexpr const char* kConfigDirName = "config";

/// RAII lock for the service mutex.
class Lock {
public:
    explicit Lock(SemaphoreHandle_t mutex) : mutex_(mutex) {
        xSemaphoreTake(mutex_, portMAX_DELAY);
    }
    ~Lock() { xSemaphoreGive(mutex_); }
    Lock(const Lock&) = delete;
    Lock& operator=(const Lock&) = delete;

private:
    SemaphoreHandle_t mutex_;
};

}  // namespace

Status ConfigService::init() {
    if (initialized_) {
        return Error::InvalidState;
    }
    if (!storage_.mounted()) {
        return Error::InvalidState;
    }

    mutex_ = xSemaphoreCreateMutex();
    if (mutex_ == nullptr) {
        return Error::NoMemory;
    }

    char dir[64];
    snprintf(dir, sizeof(dir), "%s/%s", storage_.basePath(), kConfigDirName);
    const Status status = storage_.makeDir(dir);
    if (!status.ok()) {
        return status;
    }

    initialized_ = true;
    Log::info(kTag, "config root: %s", dir);
    return Status::success();
}

void ConfigService::path(const char* ns, char* out, size_t cap) const {
    snprintf(out, cap, "%s/%s/%s.json", storage_.basePath(), kConfigDirName,
             ns);
}

/// Load a namespace document, or an empty object when the file does
/// not exist or fails to parse. Caller owns the returned cJSON*.
void* ConfigService::loadNamespace(const char* ns) const {
    char file_path[96];
    path(ns, file_path, sizeof(file_path));

    // Transient parse buffer; config access is rare (see header note).
    auto buf = std::make_unique<char[]>(kMaxFileBytes + 1);
    const Result<size_t> read =
        storage_.readFile(file_path, buf.get(), kMaxFileBytes);
    if (!read.ok()) {
        return cJSON_CreateObject();
    }
    buf[read.valueOr(0)] = '\0';

    cJSON* doc = cJSON_Parse(buf.get());
    if (doc == nullptr || !cJSON_IsObject(doc)) {
        Log::warn(kTag, "invalid JSON in %s, starting fresh", file_path);
        cJSON_Delete(doc);
        return cJSON_CreateObject();
    }
    return doc;
}

Status ConfigService::saveNamespace(const char* ns, void* doc) {
    char file_path[96];
    path(ns, file_path, sizeof(file_path));

    char* printed = cJSON_PrintUnformatted(static_cast<cJSON*>(doc));
    if (printed == nullptr) {
        return Error::NoMemory;
    }
    const Status status =
        storage_.writeFile(file_path, printed, strlen(printed));
    cJSON_free(printed);
    return status;
}

void ConfigService::publishChanged(const char* ns, const char* key) {
    ChangedEvent payload = {};
    snprintf(payload.ns, sizeof(payload.ns), "%s", ns);
    snprintf(payload.key, sizeof(payload.key), "%s", key);
    (void)events_.publish(Event::make(EventId::ConfigChanged, payload));
}

int32_t ConfigService::getInt(const char* ns, const char* key,
                              int32_t def) const {
    if (!initialized_ || ns == nullptr || key == nullptr) {
        return def;
    }
    Lock lock(mutex_);
    cJSON* doc = static_cast<cJSON*>(loadNamespace(ns));
    const cJSON* item = cJSON_GetObjectItemCaseSensitive(doc, key);
    const int32_t value =
        cJSON_IsNumber(item) ? static_cast<int32_t>(item->valuedouble) : def;
    cJSON_Delete(doc);
    return value;
}

bool ConfigService::getBool(const char* ns, const char* key, bool def) const {
    if (!initialized_ || ns == nullptr || key == nullptr) {
        return def;
    }
    Lock lock(mutex_);
    cJSON* doc = static_cast<cJSON*>(loadNamespace(ns));
    const cJSON* item = cJSON_GetObjectItemCaseSensitive(doc, key);
    const bool value = cJSON_IsBool(item) ? cJSON_IsTrue(item) : def;
    cJSON_Delete(doc);
    return value;
}

float ConfigService::getFloat(const char* ns, const char* key,
                              float def) const {
    if (!initialized_ || ns == nullptr || key == nullptr) {
        return def;
    }
    Lock lock(mutex_);
    cJSON* doc = static_cast<cJSON*>(loadNamespace(ns));
    const cJSON* item = cJSON_GetObjectItemCaseSensitive(doc, key);
    const float value =
        cJSON_IsNumber(item) ? static_cast<float>(item->valuedouble) : def;
    cJSON_Delete(doc);
    return value;
}

Status ConfigService::getString(const char* ns, const char* key, char* out,
                                size_t cap, const char* def) const {
    if (out == nullptr || cap == 0) {
        return Error::InvalidArg;
    }
    if (!initialized_ || ns == nullptr || key == nullptr) {
        snprintf(out, cap, "%s", def != nullptr ? def : "");
        return Status::success();
    }
    Lock lock(mutex_);
    cJSON* doc = static_cast<cJSON*>(loadNamespace(ns));
    const cJSON* item = cJSON_GetObjectItemCaseSensitive(doc, key);
    const char* value = cJSON_IsString(item) ? item->valuestring
                        : (def != nullptr ? def : "");
    snprintf(out, cap, "%s", value);
    cJSON_Delete(doc);
    return Status::success();
}

Status ConfigService::setNumber(const char* ns, const char* key,
                                double value) {
    if (!initialized_) {
        return Error::InvalidState;
    }
    if (ns == nullptr || key == nullptr) {
        return Error::InvalidArg;
    }
    Status status = Error::Internal;
    {
        Lock lock(mutex_);
        cJSON* doc = static_cast<cJSON*>(loadNamespace(ns));
        cJSON_DeleteItemFromObjectCaseSensitive(doc, key);
        if (cJSON_AddNumberToObject(doc, key, value) != nullptr) {
            status = saveNamespace(ns, doc);
        } else {
            status = Error::NoMemory;
        }
        cJSON_Delete(doc);
    }
    if (status.ok()) {
        publishChanged(ns, key);
    }
    return status;
}

Status ConfigService::setInt(const char* ns, const char* key, int32_t value) {
    return setNumber(ns, key, static_cast<double>(value));
}

Status ConfigService::setFloat(const char* ns, const char* key, float value) {
    return setNumber(ns, key, static_cast<double>(value));
}

Status ConfigService::setBool(const char* ns, const char* key, bool value) {
    if (!initialized_) {
        return Error::InvalidState;
    }
    if (ns == nullptr || key == nullptr) {
        return Error::InvalidArg;
    }
    Status status = Error::Internal;
    {
        Lock lock(mutex_);
        cJSON* doc = static_cast<cJSON*>(loadNamespace(ns));
        cJSON_DeleteItemFromObjectCaseSensitive(doc, key);
        if (cJSON_AddBoolToObject(doc, key, value) != nullptr) {
            status = saveNamespace(ns, doc);
        } else {
            status = Error::NoMemory;
        }
        cJSON_Delete(doc);
    }
    if (status.ok()) {
        publishChanged(ns, key);
    }
    return status;
}

Status ConfigService::setString(const char* ns, const char* key,
                                const char* value) {
    if (!initialized_) {
        return Error::InvalidState;
    }
    if (ns == nullptr || key == nullptr || value == nullptr) {
        return Error::InvalidArg;
    }
    Status status = Error::Internal;
    {
        Lock lock(mutex_);
        cJSON* doc = static_cast<cJSON*>(loadNamespace(ns));
        cJSON_DeleteItemFromObjectCaseSensitive(doc, key);
        if (cJSON_AddStringToObject(doc, key, value) != nullptr) {
            status = saveNamespace(ns, doc);
        } else {
            status = Error::NoMemory;
        }
        cJSON_Delete(doc);
    }
    if (status.ok()) {
        publishChanged(ns, key);
    }
    return status;
}

Status ConfigService::erase(const char* ns, const char* key) {
    if (!initialized_) {
        return Error::InvalidState;
    }
    if (ns == nullptr || key == nullptr) {
        return Error::InvalidArg;
    }
    Status status;
    {
        Lock lock(mutex_);
        cJSON* doc = static_cast<cJSON*>(loadNamespace(ns));
        if (cJSON_GetObjectItemCaseSensitive(doc, key) == nullptr) {
            cJSON_Delete(doc);
            return Error::NotFound;
        }
        cJSON_DeleteItemFromObjectCaseSensitive(doc, key);
        status = saveNamespace(ns, doc);
        cJSON_Delete(doc);
    }
    if (status.ok()) {
        publishChanged(ns, key);
    }
    return status;
}

void* ConfigService::exportNamespace(const char* ns, bool redact) const {
    if (!initialized_ || ns == nullptr) {
        return cJSON_CreateObject();
    }
    Lock lock(mutex_);
    cJSON* doc = static_cast<cJSON*>(loadNamespace(ns));
    cJSON* copy = cJSON_Duplicate(doc, 1);
    cJSON_Delete(doc);
    if (copy == nullptr) {
        return cJSON_CreateObject();
    }
    if (!redact) {
        return copy;
    }
    if (strcmp(ns, "system") == 0) {
        cJSON* tok = cJSON_GetObjectItemCaseSensitive(copy, "api_token");
        if (cJSON_IsString(tok) && tok->valuestring != nullptr &&
            tok->valuestring[0] != '\0') {
            cJSON_SetValuestring(tok, "(set)");
        }
    }
    if (strcmp(ns, "wifi") == 0) {
        cJSON* pw = cJSON_GetObjectItemCaseSensitive(copy, "password");
        if (cJSON_IsString(pw) && pw->valuestring != nullptr &&
            pw->valuestring[0] != '\0') {
            cJSON_SetValuestring(pw, "(set)");
        }
    }
    return copy;
}

}  // namespace gallus::services
