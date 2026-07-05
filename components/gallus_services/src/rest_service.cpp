#include "gallus/services/rest_service.hpp"

#include <cstdio>
#include <cstring>

#include "gallus/log.hpp"

namespace gallus::services {

namespace {
constexpr const char* kTag = "REST";

// Constant-time equality over a fixed length: compares every byte of
// two NUL-terminated, zero-padded buffers so the running time does not
// depend on how many leading bytes match (no early-exit like strcmp).
bool constantTimeEqual(const char* a, const char* b, size_t n) {
    unsigned char diff = 0;
    for (size_t i = 0; i < n; ++i) {
        diff |= static_cast<unsigned char>(a[i]) ^
                static_cast<unsigned char>(b[i]);
    }
    return diff == 0;
}
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

    // Keep token_ in sync with config writes from any source (config
    // API, factory reset, provisioning) so a saved token is enforced
    // immediately, without waiting for a reboot.
    (void)events_.subscribe(EventId::ConfigChanged,
                            &RestService::onConfigChanged, this);

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
        Log::error(kTag, "route %s registration failed: %s (%u/%u slots used)",
                   uri, esp_err_to_name(err),
                   static_cast<unsigned>(routes_used_),
                   static_cast<unsigned>(kMaxRoutes));
        return fromEspErr(err);
    }
    ++routes_used_;
    Log::debug(kTag, "route registered: %s (%u/%u)", uri,
               static_cast<unsigned>(routes_used_),
               static_cast<unsigned>(kMaxRoutes));
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
        Log::error(kTag,
                   "websocket %s registration failed: %s (%u/%u slots used)",
                   uri, esp_err_to_name(err),
                   static_cast<unsigned>(routes_used_),
                   static_cast<unsigned>(kMaxRoutes));
        return fromEspErr(err);
    }
    ++routes_used_;
    Log::debug(kTag, "websocket registered: %s", uri);
    return Status::success();
}

Status RestService::unregisterRoute(httpd_method_t method, const char* uri) {
    if (server_ == nullptr) {
        return Error::InvalidState;
    }
    const esp_err_t err = httpd_unregister_uri_handler(server_, uri, method);
    if (err == ESP_OK && routes_used_ > 0) {
        --routes_used_;
    }
    return fromEspErr(err);
}

bool RestService::bearerAuthorized(httpd_req_t* req) const {
    char header[96] = {};
    if (httpd_req_get_hdr_value_str(req, "Authorization", header,
                                    sizeof(header)) != ESP_OK) {
        return false;
    }
    const char* kPrefix = "Bearer ";
    if (strncmp(header, kPrefix, strlen(kPrefix)) != 0) {
        return false;
    }
    // Compare in constant time over the full token buffer: copy the
    // candidate into a fixed zero-padded buffer so every byte read is
    // in-bounds and the timing does not leak the match length. The
    // buffer is one byte larger than token_ so a presented token longer
    // than the stored one keeps a non-NUL at index sizeof(token_)-1 and
    // is rejected (rather than truncate-matching a max-length token).
    const char* presented = header + strlen(kPrefix);
    char candidate[sizeof(token_) + 1] = {};
    for (size_t i = 0; i < sizeof(candidate) - 1 && presented[i] != '\0'; ++i) {
        candidate[i] = presented[i];
    }
    return constantTimeEqual(candidate, token_, sizeof(token_));
}

bool RestService::authorize(httpd_req_t* req) const {
    if (token_[0] == '\0') {
        return true;  // open access (warned at init)
    }
    if (bearerAuthorized(req)) {
        return true;
    }

    httpd_resp_set_status(req, "401 Unauthorized");
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, "{\"error\":\"unauthorized\"}");
    return false;
}

bool RestService::authorizeWs(httpd_req_t* req) const {
    if (token_[0] == '\0') {
        return true;
    }
    if (bearerAuthorized(req)) {
        return true;  // non-browser clients can send the header
    }

    // Browsers cannot set headers on a WebSocket handshake, so also
    // accept the token as a query parameter: /ws?token=<token>.
    char query[160] = {};
    char token[sizeof(token_)] = {};
    if (httpd_req_get_url_query_str(req, query, sizeof(query)) == ESP_OK &&
        httpd_query_key_value(query, "token", token, sizeof(token)) ==
            ESP_OK &&
        constantTimeEqual(token, token_, sizeof(token_))) {
        return true;
    }
    return false;
}

void RestService::reloadToken() {
    (void)config_.getString("system", "api_token", token_, sizeof(token_), "");
}

void RestService::onConfigChanged(const Event& event, void* ctx) {
    auto* self = static_cast<RestService*>(ctx);
    const auto* changed = event.as<ConfigService::ChangedEvent>();
    if (changed == nullptr) {
        return;
    }
    if (strcmp(changed->ns, "system") == 0 &&
        strcmp(changed->key, "api_token") == 0) {
        self->reloadToken();
        Log::info(kTag, "api_token updated — auth is now %s",
                  self->token_[0] == '\0' ? "OPEN" : "enforced");
    }
}

}  // namespace gallus::services
