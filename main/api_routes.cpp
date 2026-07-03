#include "api_routes.hpp"

#include <cstring>

#include "cJSON.h"
#include "esp_idf_version.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_netif_ip_addr.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "gallus/version.hpp"

namespace gallus::app {

namespace {

constexpr size_t kMaxFileReadBytes = 8192;

void delayedRestart(void* /*ctx*/) {
    vTaskDelay(pdMS_TO_TICKS(400));
    esp_restart();
}

void scheduleRestart() {
    xTaskCreate(&delayedRestart, "gallus_reboot", 2048, nullptr, 5, nullptr);
}

void fillHostname(const services::ConfigService* config, char* out,
                  size_t cap) {
    char fallback[32] = {};
    uint8_t mac[6] = {};
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    snprintf(fallback, sizeof(fallback), "gallus-%02x%02x", mac[4], mac[5]);
    if (config != nullptr) {
        (void)config->getString("network", "hostname", out, cap, fallback);
    } else {
        snprintf(out, cap, "%s", fallback);
    }
}

void addNetworkFields(cJSON* doc, const ApiContext* ctx) {
    const bool charge =
        ctx->power != nullptr && ctx->power->chargeMode();
    cJSON_AddBoolToObject(doc, "wifi_connected", false);
    cJSON_AddStringToObject(doc, "ip", "");
    cJSON_AddStringToObject(doc, "wifi_ssid", "");

    if (charge) {
        cJSON_AddStringToObject(doc, "wifi_status", "charge mode");
        return;
    }

    char ssid[33] = {};
    if (ctx->config != nullptr) {
        (void)ctx->config->getString("wifi", "ssid", ssid, sizeof(ssid), "");
    }
    if (ssid[0] != '\0') {
        cJSON_AddStringToObject(doc, "wifi_ssid", ssid);
    }

    esp_netif_t* sta = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    if (sta == nullptr) {
        cJSON_AddStringToObject(doc, "wifi_status", "disconnected");
        return;
    }

    esp_netif_ip_info_t ip_info = {};
    if (esp_netif_get_ip_info(sta, &ip_info) != ESP_OK ||
        ip_info.ip.addr == 0) {
        cJSON_AddStringToObject(doc, "wifi_status", "connecting");
        return;
    }

    char ip_str[16] = {};
    snprintf(ip_str, sizeof(ip_str), IPSTR, IP2STR(&ip_info.ip));
    cJSON_AddStringToObject(doc, "ip", ip_str);
    cJSON_ReplaceItemInObject(doc, "wifi_connected",
                              cJSON_CreateBool(true));
    cJSON_AddStringToObject(doc, "wifi_status", "connected");
}

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
    char hostname[32] = {};
    fillHostname(ctx->config, hostname, sizeof(hostname));
    cJSON_AddStringToObject(doc, "hostname", hostname);
    if (ctx->power != nullptr) {
        cJSON_AddBoolToObject(doc, "charge_mode", ctx->power->chargeMode());
    }
    addNetworkFields(doc, ctx);
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

    cJSON* doc = ctx->diagnostics->snapshotJson();
    if (doc == nullptr) {
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR,
                                   "out of memory");
    }
    return sendJson(req, doc);
}

bool queryNamespace(httpd_req_t* req, char* ns, size_t cap) {
    char query[64] = {};
    if (httpd_req_get_url_query_len(req) + 1 > sizeof(query)) {
        return false;
    }
    if (httpd_req_get_url_query_str(req, query, sizeof(query)) != ESP_OK) {
        return false;
    }
    return httpd_query_key_value(query, "namespace", ns, cap) == ESP_OK &&
           ns[0] != '\0';
}

esp_err_t configGetHandler(httpd_req_t* req) {
    auto* ctx = static_cast<ApiContext*>(req->user_ctx);
    if (!ctx->rest->authorize(req)) {
        return ESP_OK;
    }

    char ns[32] = {};
    if (!queryNamespace(req, ns, sizeof(ns))) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST,
                                   "missing namespace");
    }

    cJSON* doc = cJSON_CreateObject();
    cJSON_AddStringToObject(doc, "namespace", ns);
    cJSON* values = static_cast<cJSON*>(ctx->config->exportNamespace(ns, true));
    cJSON_AddItemToObject(doc, "values", values);
    return sendJson(req, doc);
}

Status applyConfigValue(services::ConfigService& config, const char* ns,
                        const char* key, const cJSON* value) {
    if (cJSON_IsBool(value)) {
        return config.setBool(ns, key, cJSON_IsTrue(value));
    }
    if (cJSON_IsNumber(value)) {
        return config.setInt(ns, key, static_cast<int32_t>(value->valuedouble));
    }
    if (cJSON_IsString(value) && value->valuestring != nullptr) {
        if (strcmp(value->valuestring, "(set)") == 0) {
            return Status::success();
        }
        return config.setString(ns, key, value->valuestring);
    }
    return Error::InvalidArg;
}

esp_err_t configPutHandler(httpd_req_t* req) {
    auto* ctx = static_cast<ApiContext*>(req->user_ctx);
    if (!ctx->rest->authorize(req)) {
        return ESP_OK;
    }

    char body[512] = {};
    const int received = httpd_req_recv(req, body, sizeof(body) - 1);
    if (received <= 0) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "empty body");
    }
    body[received] = '\0';

    cJSON* doc = cJSON_Parse(body);
    if (doc == nullptr || !cJSON_IsObject(doc)) {
        cJSON_Delete(doc);
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "invalid json");
    }

    const cJSON* ns_item = cJSON_GetObjectItemCaseSensitive(doc, "namespace");
    const cJSON* values = cJSON_GetObjectItemCaseSensitive(doc, "values");
    if (!cJSON_IsString(ns_item) || ns_item->valuestring == nullptr ||
        !cJSON_IsObject(values)) {
        cJSON_Delete(doc);
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST,
                                   "need namespace and values");
    }

    const char* ns = ns_item->valuestring;
    Status status = Status::success();
    cJSON* child = nullptr;
    cJSON_ArrayForEach(child, values) {
        if (child->string == nullptr) {
            continue;
        }
        status = applyConfigValue(*ctx->config, ns, child->string, child);
        if (!status.ok()) {
            break;
        }
    }
    cJSON_Delete(doc);

    if (!status.ok()) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "update failed");
    }
    if (strcmp(ns, "system") == 0) {
        ctx->rest->reloadToken();
    }

    cJSON* out = cJSON_CreateObject();
    cJSON_AddBoolToObject(out, "ok", true);
    cJSON_AddStringToObject(out, "namespace", ns);
    cJSON_AddBoolToObject(out, "reboot_recommended",
                          strcmp(ns, "network") == 0 ||
                              strcmp(ns, "wifi") == 0);
    return sendJson(req, out);
}

esp_err_t rebootHandler(httpd_req_t* req) {
    auto* ctx = static_cast<ApiContext*>(req->user_ctx);
    if (!ctx->rest->authorize(req)) {
        return ESP_OK;
    }

    cJSON* doc = cJSON_CreateObject();
    cJSON_AddBoolToObject(doc, "ok", true);
    cJSON_AddStringToObject(doc, "action", "reboot");
    const esp_err_t err = sendJson(req, doc);
    if (err == ESP_OK) {
        scheduleRestart();
    }
    return err;
}

esp_err_t resetHandler(httpd_req_t* req) {
    auto* ctx = static_cast<ApiContext*>(req->user_ctx);
    if (!ctx->rest->authorize(req)) {
        return ESP_OK;
    }

    const Status reset = ctx->config->resetAll();
    if (!reset.ok()) {
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR,
                                   "reset failed");
    }

    cJSON* doc = cJSON_CreateObject();
    cJSON_AddBoolToObject(doc, "ok", true);
    cJSON_AddStringToObject(doc, "action", "reset");
    const esp_err_t err = sendJson(req, doc);
    if (err == ESP_OK) {
        scheduleRestart();
    }
    return err;
}

esp_err_t chargeModeHandler(httpd_req_t* req) {
    auto* ctx = static_cast<ApiContext*>(req->user_ctx);
    if (!ctx->rest->authorize(req)) {
        return ESP_OK;
    }
    if (ctx->power == nullptr) {
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR,
                                   "power service unavailable");
    }

    bool enable = true;
    if (req->content_len > 0) {
        char body[64] = {};
        const int received =
            httpd_req_recv(req, body, sizeof(body) - 1);
        if (received > 0) {
            body[received] = '\0';
            cJSON* doc = cJSON_Parse(body);
            if (doc != nullptr) {
                const cJSON* item =
                    cJSON_GetObjectItemCaseSensitive(doc, "enable");
                if (cJSON_IsBool(item)) {
                    enable = cJSON_IsTrue(item);
                }
                cJSON_Delete(doc);
            }
        }
    }

    const Status status =
        enable ? ctx->power->enterChargeMode()
               : ctx->power->exitChargeMode();
    if (!status.ok()) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST,
                                   status.message());
    }

    cJSON* out = cJSON_CreateObject();
    cJSON_AddBoolToObject(out, "ok", true);
    cJSON_AddBoolToObject(out, "charge_mode", enable);
    return sendJson(req, out);
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
        {"GET", "/api/v1/config?namespace=system", "Read a config namespace"},
        {"PUT", "/api/v1/config", "Update config namespace values"},
        {"POST", "/api/v1/system/reboot", "Reboot the device"},
        {"POST", "/api/v1/system/reset", "Erase saved settings and reboot"},
        {"POST", "/api/v1/system/charge-mode", "Enter or exit charge mode"},
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
    status = ctx.rest->registerRoute(HTTP_GET, "/api/v1/config",
                                     &configGetHandler, &ctx);
    if (!status.ok()) {
        return status;
    }
    status = ctx.rest->registerRoute(HTTP_PUT, "/api/v1/config",
                                     &configPutHandler, &ctx);
    if (!status.ok()) {
        return status;
    }
    status = ctx.rest->registerRoute(HTTP_POST, "/api/v1/system/reboot",
                                     &rebootHandler, &ctx);
    if (!status.ok()) {
        return status;
    }
    status = ctx.rest->registerRoute(HTTP_POST, "/api/v1/system/reset",
                                     &resetHandler, &ctx);
    if (!status.ok()) {
        return status;
    }
    status = ctx.rest->registerRoute(HTTP_POST, "/api/v1/system/charge-mode",
                                     &chargeModeHandler, &ctx);
    if (!status.ok()) {
        return status;
    }
    return ctx.rest->registerRoute(HTTP_GET, "/api/v1/endpoints",
                                   &endpointsHandler, &ctx);
}

}  // namespace gallus::app
