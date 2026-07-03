#include "api_routes.hpp"

#include <cstring>

#include "cJSON.h"
#include "esp_idf_version.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "gallus/version.hpp"

namespace gallus::app {

namespace {

constexpr size_t kMaxFileReadBytes = 8192;

/// Serialize @p doc, send it, and free everything.
esp_err_t sendJson(httpd_req_t* req, cJSON* doc) {
    char* printed = cJSON_PrintUnformatted(doc);
    cJSON_Delete(doc);
    if (printed == nullptr) {
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR,
                                   "out of memory");
    }
    httpd_resp_set_type(req, "application/json");
    const esp_err_t err = httpd_resp_sendstr(req, printed);
    cJSON_free(printed);
    return err;
}

esp_err_t systemHandler(httpd_req_t* req) {
    auto* ctx = static_cast<ApiContext*>(req->user_ctx);
    if (!ctx->rest->authorize(req)) {
        return ESP_OK;
    }

    cJSON* doc = cJSON_CreateObject();
    cJSON_AddStringToObject(doc, "name", "GallusOS");
    cJSON_AddStringToObject(doc, "version", kVersion);
    cJSON_AddStringToObject(doc, "idf", IDF_VER);
    cJSON_AddNumberToObject(doc, "uptime_ms",
                            static_cast<double>(esp_timer_get_time() / 1000));
    cJSON_AddNumberToObject(doc, "free_heap", esp_get_free_heap_size());
    cJSON_AddNumberToObject(doc, "min_free_heap",
                            esp_get_minimum_free_heap_size());
    cJSON_AddNumberToObject(
        doc, "boot_count", ctx->config->getInt("system", "boot_count", 0));
    return sendJson(req, doc);
}

const char* pinStateName(services::GpioService::PinState state) {
    using PinState = services::GpioService::PinState;
    switch (state) {
        case PinState::Free:      return "free";
        case PinState::Allocated: return "allocated";
        case PinState::Reserved:  return "reserved";
        case PinState::Critical:  return "critical";
        case PinState::Invalid:   break;
    }
    return "invalid";
}

esp_err_t gpioHandler(httpd_req_t* req) {
    auto* ctx = static_cast<ApiContext*>(req->user_ctx);
    if (!ctx->rest->authorize(req)) {
        return ESP_OK;
    }

    services::GpioService::PinInfo pins[24];
    const size_t count =
        ctx->gpio->snapshot(pins, sizeof(pins) / sizeof(pins[0]));

    cJSON* doc = cJSON_CreateObject();
    cJSON* list = cJSON_AddArrayToObject(doc, "pins");
    for (size_t i = 0; i < count; ++i) {
        cJSON* row = cJSON_CreateObject();
        cJSON_AddNumberToObject(row, "pin", pins[i].pin);
        cJSON_AddStringToObject(row, "state", pinStateName(pins[i].state));
        cJSON_AddStringToObject(row, "owner", pins[i].owner);
        cJSON_AddItemToArray(list, row);
    }
    return sendJson(req, doc);
}

esp_err_t modulesHandler(httpd_req_t* req) {
    auto* ctx = static_cast<ApiContext*>(req->user_ctx);
    if (!ctx->rest->authorize(req)) {
        return ESP_OK;
    }

    cJSON* doc = cJSON_CreateObject();
    cJSON* list = cJSON_AddArrayToObject(doc, "modules");
    for (size_t i = 0; i < ctx->modules->count(); ++i) {
        const sdk::ModuleManager::Entry& entry = ctx->modules->at(i);
        cJSON* row = cJSON_CreateObject();
        cJSON_AddStringToObject(row, "name", entry.info->name);
        cJSON_AddStringToObject(row, "version", entry.info->version);
        cJSON_AddStringToObject(row, "category", entry.info->category);
        cJSON_AddStringToObject(row, "description", entry.info->description);
        cJSON_AddStringToObject(
            row, "state", sdk::ModuleManager::stateName(entry.state));
        cJSON_AddItemToArray(list, row);
    }
    return sendJson(req, doc);
}

esp_err_t batteryHandler(httpd_req_t* req) {
    auto* ctx = static_cast<ApiContext*>(req->user_ctx);
    if (!ctx->rest->authorize(req)) {
        return ESP_OK;
    }

    cJSON* doc = cJSON_CreateObject();
    cJSON_AddNumberToObject(doc, "millivolts", ctx->battery->millivolts());
    cJSON_AddNumberToObject(doc, "percent", ctx->battery->percent());
    return sendJson(req, doc);
}

bool isSafeFsPath(const char* path) {
    if (path == nullptr || path[0] != '/') {
        return false;
    }
    if (strncmp(path, services::StorageService::kBasePath,
                strlen(services::StorageService::kBasePath)) != 0) {
        return false;
    }
    return strstr(path, "..") == nullptr;
}

bool queryPath(httpd_req_t* req, char* path, size_t cap) {
    char query[128] = {};
    if (httpd_req_get_url_query_len(req) + 1 > sizeof(query)) {
        return false;
    }
    if (httpd_req_get_url_query_str(req, query, sizeof(query)) != ESP_OK) {
        return false;
    }
    if (httpd_query_key_value(query, "path", path, cap) != ESP_OK) {
        return false;
    }
    return isSafeFsPath(path);
}

esp_err_t diagnosticsHandler(httpd_req_t* req) {
    auto* ctx = static_cast<ApiContext*>(req->user_ctx);
    if (!ctx->rest->authorize(req)) {
        return ESP_OK;
    }

    cJSON* doc = cJSON_CreateObject();

    cJSON* heap = cJSON_AddObjectToObject(doc, "heap");
    cJSON_AddNumberToObject(heap, "free", esp_get_free_heap_size());
    cJSON_AddNumberToObject(heap, "min_free", esp_get_minimum_free_heap_size());

    if (ctx->kernel != nullptr) {
        cJSON* events = cJSON_AddObjectToObject(doc, "events");
        cJSON_AddNumberToObject(events, "delivered",
                                ctx->kernel->events().deliveredCount());
        cJSON_AddNumberToObject(events, "dropped",
                                ctx->kernel->events().droppedCount());

        cJSON* scheduler = cJSON_AddObjectToObject(doc, "scheduler");
        cJSON_AddNumberToObject(scheduler, "active_jobs",
                                ctx->kernel->scheduler().activeJobs());
    }

    if (ctx->storage != nullptr && ctx->storage->mounted()) {
        cJSON* fs = cJSON_AddObjectToObject(doc, "filesystem");
        const auto total = ctx->storage->totalBytes();
        const auto used = ctx->storage->usedBytes();
        if (total.ok()) {
            cJSON_AddNumberToObject(fs, "total_bytes", total.value());
        }
        if (used.ok()) {
            cJSON_AddNumberToObject(fs, "used_bytes", used.value());
        }
    }

    cJSON* tasks = cJSON_AddObjectToObject(doc, "tasks");
    cJSON_AddNumberToObject(tasks, "count", uxTaskGetNumberOfTasks());
#if CONFIG_FREERTOS_USE_TRACE_FACILITY && \
    CONFIG_FREERTOS_USE_STATS_FORMATTING_FUNCTIONS
    char task_buf[1024] = {};
    vTaskList(task_buf);
    cJSON_AddStringToObject(tasks, "list", task_buf);
#endif

    return sendJson(req, doc);
}

esp_err_t filesListHandler(httpd_req_t* req) {
    auto* ctx = static_cast<ApiContext*>(req->user_ctx);
    if (!ctx->rest->authorize(req)) {
        return ESP_OK;
    }

    char path[96];
    snprintf(path, sizeof(path), "%s", services::StorageService::kBasePath);
    char query_path[96] = {};
    if (httpd_req_get_url_query_len(req) > 0 &&
        queryPath(req, query_path, sizeof(query_path))) {
        snprintf(path, sizeof(path), "%s", query_path);
    }

    services::DirEntry entries[32];
    const auto listed =
        ctx->storage->listDir(path, entries, sizeof(entries) / sizeof(entries[0]));
    if (!listed.ok()) {
        return httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "path not found");
    }

    cJSON* doc = cJSON_CreateObject();
    cJSON_AddStringToObject(doc, "path", path);
    cJSON* list = cJSON_AddArrayToObject(doc, "entries");
    for (size_t i = 0; i < listed.value(); ++i) {
        cJSON* row = cJSON_CreateObject();
        cJSON_AddStringToObject(row, "name", entries[i].name);
        cJSON_AddBoolToObject(row, "dir", entries[i].is_dir);
        cJSON_AddNumberToObject(row, "size", entries[i].size);
        cJSON_AddItemToArray(list, row);
    }
    return sendJson(req, doc);
}

esp_err_t filesReadHandler(httpd_req_t* req) {
    auto* ctx = static_cast<ApiContext*>(req->user_ctx);
    if (!ctx->rest->authorize(req)) {
        return ESP_OK;
    }

    char path[96] = {};
    if (!queryPath(req, path, sizeof(path))) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "missing path");
    }

    char buf[kMaxFileReadBytes];
    const auto read = ctx->storage->readFile(path, buf, sizeof(buf) - 1);
    if (!read.ok()) {
        return httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "not found");
    }
    buf[read.value()] = '\0';

    cJSON* doc = cJSON_CreateObject();
    cJSON_AddStringToObject(doc, "path", path);
    cJSON_AddNumberToObject(doc, "size", read.value());
    cJSON_AddStringToObject(doc, "content", buf);
    return sendJson(req, doc);
}

esp_err_t i2cScanHandler(httpd_req_t* req) {
    auto* ctx = static_cast<ApiContext*>(req->user_ctx);
    if (!ctx->rest->authorize(req)) {
        return ESP_OK;
    }

    if (ctx->i2c == nullptr || !ctx->i2c->ready()) {
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR,
                                   "i2c not ready");
    }

    uint8_t addrs[services::I2cService::kMaxScanResults];
    const auto found = ctx->i2c->scan(addrs, sizeof(addrs));
    if (!found.ok()) {
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR,
                                   "scan failed");
    }

    cJSON* doc = cJSON_CreateObject();
    cJSON* list = cJSON_AddArrayToObject(doc, "addresses");
    for (size_t i = 0; i < found.value(); ++i) {
        cJSON_AddItemToArray(list, cJSON_CreateNumber(addrs[i]));
    }
    return sendJson(req, doc);
}

esp_err_t endpointsHandler(httpd_req_t* req) {
    auto* ctx = static_cast<ApiContext*>(req->user_ctx);
    if (!ctx->rest->authorize(req)) {
        return ESP_OK;
    }

    cJSON* doc = cJSON_CreateObject();
    cJSON* list = cJSON_AddArrayToObject(doc, "endpoints");
    struct Row {
        const char* method;
        const char* path;
        const char* description;
    };
    static constexpr Row kRows[] = {
        {"GET", "/api/v1/system", "System information"},
        {"GET", "/api/v1/gpio", "GPIO reservation snapshot"},
        {"GET", "/api/v1/modules", "Loaded modules"},
        {"GET", "/api/v1/battery", "Battery voltage and percentage"},
        {"GET", "/api/v1/diagnostics", "Heap, tasks, event bus stats"},
        {"GET", "/api/v1/files/list?path=/fs", "List directory entries"},
        {"GET", "/api/v1/files/read?path=/fs/...", "Read a text file"},
        {"GET", "/api/v1/i2c/scan", "Scan the I2C bus"},
        {"GET", "/api/v1/endpoints", "This list"},
        {"POST", "/api/v1/ota/upload", "Upload firmware binary"},
    };
    for (const Row& row : kRows) {
        cJSON* item = cJSON_CreateObject();
        cJSON_AddStringToObject(item, "method", row.method);
        cJSON_AddStringToObject(item, "path", row.path);
        cJSON_AddStringToObject(item, "description", row.description);
        cJSON_AddItemToArray(list, item);
    }
    return sendJson(req, doc);
}

}  // namespace

Status registerApiRoutes(ApiContext& ctx) {
    Status status = ctx.rest->registerRoute(HTTP_GET, "/api/v1/system",
                                            &systemHandler, &ctx);
    if (!status.ok()) {
        return status;
    }
    status = ctx.rest->registerRoute(HTTP_GET, "/api/v1/gpio", &gpioHandler,
                                     &ctx);
    if (!status.ok()) {
        return status;
    }
    status = ctx.rest->registerRoute(HTTP_GET, "/api/v1/modules",
                                     &modulesHandler, &ctx);
    if (!status.ok()) {
        return status;
    }
    status = ctx.rest->registerRoute(HTTP_GET, "/api/v1/battery",
                                     &batteryHandler, &ctx);
    if (!status.ok()) {
        return status;
    }
    status = ctx.rest->registerRoute(HTTP_GET, "/api/v1/diagnostics",
                                     &diagnosticsHandler, &ctx);
    if (!status.ok()) {
        return status;
    }
    status = ctx.rest->registerRoute(HTTP_GET, "/api/v1/files/list",
                                     &filesListHandler, &ctx);
    if (!status.ok()) {
        return status;
    }
    status = ctx.rest->registerRoute(HTTP_GET, "/api/v1/files/read",
                                     &filesReadHandler, &ctx);
    if (!status.ok()) {
        return status;
    }
    status = ctx.rest->registerRoute(HTTP_GET, "/api/v1/i2c/scan",
                                     &i2cScanHandler, &ctx);
    if (!status.ok()) {
        return status;
    }
    return ctx.rest->registerRoute(HTTP_GET, "/api/v1/endpoints",
                                   &endpointsHandler, &ctx);
}

}  // namespace gallus::app
