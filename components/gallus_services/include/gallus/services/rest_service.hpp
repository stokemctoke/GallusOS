#pragma once

#include <cstddef>

#include "esp_http_server.h"

#include "gallus/error.hpp"
#include "gallus/services/config_service.hpp"

/// @file rest_service.hpp
/// @brief The HTTP/REST front door.
///
/// Owns the single httpd instance on port 80. Services register their
/// routes here; the WebUI, the provisioning portal and all future
/// clients go through this server. API routes live under /api/v1/.
///
/// Authentication: when the config key system/api_token is set, API
/// handlers must call authorize() and bail out when it returns false
/// (it sends the 401 itself). An empty token means open access — a
/// warning is logged at startup.

namespace gallus::services {

class RestService {
public:
    using Handler = esp_err_t (*)(httpd_req_t* req);

    static constexpr size_t kMaxRoutes = 28;

    explicit RestService(ConfigService& config) : config_(config) {}
    RestService(const RestService&) = delete;
    RestService& operator=(const RestService&) = delete;

    /// Start the HTTP server. Call after esp_netif is initialized.
    Status init();

    /// Register @p handler for @p method on @p uri. Wildcards allowed
    /// (e.g. "/*"). @p ctx becomes httpd_req_t::user_ctx.
    Status registerRoute(httpd_method_t method, const char* uri,
                         Handler handler, void* ctx = nullptr);

    /// Register a WebSocket endpoint at @p uri. @p handler is invoked
    /// for both the handshake (HTTP_GET) and subsequent frames.
    Status registerWebSocket(const char* uri, Handler handler,
                             void* ctx = nullptr);

    /// Remove a previously registered route.
    Status unregisterRoute(httpd_method_t method, const char* uri);

    /// Enforce token auth. Returns true when the request may proceed;
    /// otherwise a 401 response has already been sent.
    bool authorize(httpd_req_t* req) const;

    /// Token check for WebSocket handshakes: accepts the Bearer header
    /// or a ?token= query parameter (browsers cannot set headers on a
    /// WebSocket). Sends no response — the caller closes the socket.
    bool authorizeWs(httpd_req_t* req) const;

    /// Reload system/api_token from config (after a settings change).
    void reloadToken();

    [[nodiscard]] bool running() const { return server_ != nullptr; }

    /// Underlying httpd handle (for WebSocket async sends). May be null.
    [[nodiscard]] httpd_handle_t server() const { return server_; }

private:
    bool bearerAuthorized(httpd_req_t* req) const;

    ConfigService& config_;
    httpd_handle_t server_ = nullptr;
    char token_[64] = {};
};

}  // namespace gallus::services
