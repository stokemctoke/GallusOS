#include "gallus/services/rest_service.hpp"

#include <cstring>

#include "gallus/log.hpp"

namespace gallus::services {

namespace {
constexpr const char* kTag = "REST";
}

Status RestService::init() {
    if (server_ != nullptr) {
        return Error::InvalidState;
    }

    (void)config_.getString("system", "api_token", token_, sizeof(token_), "");
    if (token_[0] == '\0') {
        Log::warn(kTag,
                  "no api_token configured — API is OPEN. Set config "
                  "system/api_token to enable auth");
    }

    httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
    cfg.max_uri_handlers = kMaxRoutes;
    cfg.uri_match_fn = httpd_uri_match_wildcard;
    cfg.lru_purge_enable = true;
    cfg.stack_size = 6144;  // headroom for cJSON in handlers

    const esp_err_t err = httpd_start(&server_, &cfg);
    if (err != ESP_OK) {
        server_ = nullptr;
        Log::error(kTag, "httpd start failed: %s", esp_err_to_name(err));
        return fromEspErr(err);
    }

    Log::info(kTag, "http server up on port %u", cfg.server_port);
    return Status::success();
}

Status RestService::registerRoute(httpd_method_t method, const char* uri,
                                  Handler handler, void* ctx) {
    if (server_ == nullptr) {
        return Error::InvalidState;
    }
    if (uri == nullptr || handler == nullptr) {
        return Error::InvalidArg;
    }

    httpd_uri_t route = {};
    route.uri = uri;
    route.method = method;
    route.handler = handler;
    route.user_ctx = ctx;

    const esp_err_t err = httpd_register_uri_handler(server_, &route);
    if (err != ESP_OK) {
        Log::error(kTag, "route %s registration failed: %s", uri,
                   esp_err_to_name(err));
        return fromEspErr(err);
    }
    Log::debug(kTag, "route registered: %s", uri);
    return Status::success();
}

Status RestService::registerWebSocket(const char* uri, Handler handler,
                                      void* ctx) {
    if (server_ == nullptr) {
        return Error::InvalidState;
    }
    if (uri == nullptr || handler == nullptr) {
        return Error::InvalidArg;
    }

    httpd_uri_t route = {};
    route.uri = uri;
    route.method = HTTP_GET;
    route.handler = handler;
    route.user_ctx = ctx;
    route.is_websocket = true;

    const esp_err_t err = httpd_register_uri_handler(server_, &route);
    if (err != ESP_OK) {
        Log::error(kTag, "websocket %s registration failed: %s", uri,
                   esp_err_to_name(err));
        return fromEspErr(err);
    }
    Log::debug(kTag, "websocket registered: %s", uri);
    return Status::success();
}

Status RestService::unregisterRoute(httpd_method_t method, const char* uri) {
    if (server_ == nullptr) {
        return Error::InvalidState;
    }
    const esp_err_t err = httpd_unregister_uri_handler(server_, uri, method);
    return fromEspErr(err);
}

bool RestService::authorize(httpd_req_t* req) const {
    if (token_[0] == '\0') {
        return true;  // open access (warned at init)
    }

    char header[96] = {};
    if (httpd_req_get_hdr_value_str(req, "Authorization", header,
                                    sizeof(header)) == ESP_OK) {
        const char* kPrefix = "Bearer ";
        if (strncmp(header, kPrefix, strlen(kPrefix)) == 0 &&
            strcmp(header + strlen(kPrefix), token_) == 0) {
            return true;
        }
    }

    httpd_resp_set_status(req, "401 Unauthorized");
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, "{\"error\":\"unauthorized\"}");
    return false;
}

}  // namespace gallus::services
