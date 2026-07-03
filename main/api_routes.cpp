#include "api_routes.hpp"

#include "cJSON.h"
#include "esp_idf_version.h"
#include "esp_system.h"
#include "esp_timer.h"

#include "gallus/version.hpp"

namespace gallus::app {

namespace {

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
    return ctx.rest->registerRoute(HTTP_GET, "/api/v1/battery",
                                   &batteryHandler, &ctx);
}

}  // namespace gallus::app
