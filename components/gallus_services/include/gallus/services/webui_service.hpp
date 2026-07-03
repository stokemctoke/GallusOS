#pragma once

#include <cstddef>
#include <cstdint>

#include "esp_http_server.h"

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#include "gallus/error.hpp"
#include "gallus/event_bus.hpp"
#include "gallus/services/rest_service.hpp"

/// @file webui_service.hpp
/// @brief Serves the web dashboard and the live WebSocket feed.
///
/// The dashboard is a single gzipped HTML page embedded in the
/// firmware (versioned and OTA-updated alongside it). It talks to the
/// device over two channels only, per the architecture:
///   - REST for request/response (the /api/v1/* routes)
///   - a WebSocket at /ws for live push (telemetry, module and OTA
///     events streamed from the event bus)
///
/// Event handlers run on the event-bus dispatcher task and hand the
/// actual socket writes to the httpd worker via httpd_queue_work, so
/// no framework task ever blocks on a slow client.

namespace gallus::services {

class WebUiService {
public:
    static constexpr size_t kMaxClients = 4;

    WebUiService(EventBus& events, RestService& rest)
        : events_(events), rest_(rest) {}
    WebUiService(const WebUiService&) = delete;
    WebUiService& operator=(const WebUiService&) = delete;

    /// Register the "/" page and "/ws" socket, subscribe to events.
    Status init();

private:
    struct WsSend;

    static esp_err_t pageHandler(httpd_req_t* req);
    static esp_err_t wsHandler(httpd_req_t* req);
    static void onEvent(const Event& event, void* ctx);
    static void wsSendWorker(void* arg);

    void addClient(int fd);
    void removeClient(int fd);
    void broadcast(const char* json);

    EventBus& events_;
    RestService& rest_;
    int clients_[kMaxClients] = {};
    size_t client_count_ = 0;
    SemaphoreHandle_t mutex_ = nullptr;
};

}  // namespace gallus::services
