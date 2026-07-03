#include "gallus/services/webui_service.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "gallus/log.hpp"
#include "gallus/services/battery_service.hpp"
#include "gallus/services/ota_service.hpp"
#include "gallus/services/wifi_service.hpp"

#include "esp_log.h"

/// Embedded gzipped dashboard (see gallus_services CMakeLists.txt).
extern const uint8_t kDashboardStart[] asm("_binary_index_html_gz_start");
extern const uint8_t kDashboardEnd[] asm("_binary_index_html_gz_end");
extern const uint8_t kFaviconPngStart[] asm("_binary_favicon_png_start");
extern const uint8_t kFaviconPngEnd[] asm("_binary_favicon_png_end");
extern const uint8_t kFaviconIcoStart[] asm("_binary_favicon_ico_start");
extern const uint8_t kFaviconIcoEnd[] asm("_binary_favicon_ico_end");

namespace gallus::services {

WebUiService* WebUiService::log_instance_ = nullptr;
vprintf_like_t WebUiService::prev_log_ = nullptr;

namespace {

constexpr const char* kTag = "WebUI";

/// Mirror of sdk::ModuleManager::ModuleEventPayload.
struct ModulePayload {
    char name[24];
};

}  // namespace

/// Work item handed to the httpd worker to push one frame to one fd.
struct WebUiService::WsSend {
    WebUiService* service;
    httpd_handle_t server;
    int fd;
    size_t len;
    char data[192];
};

void WebUiService::wsSendWorker(void* arg) {
    auto* work = static_cast<WsSend*>(arg);
    httpd_ws_frame_t frame = {};
    frame.type = HTTPD_WS_TYPE_TEXT;
    frame.payload = reinterpret_cast<uint8_t*>(work->data);
    frame.len = work->len;
    if (httpd_ws_send_frame_async(work->server, work->fd, &frame) != ESP_OK) {
        work->service->removeClient(work->fd);
    }
    free(work);
}

int WebUiService::logVprintf(const char* fmt, va_list args) {
    if (log_instance_ != nullptr) {
        char line[220] = {};
        va_list copy;
        va_copy(copy, args);
        vsnprintf(line, sizeof(line), fmt, copy);
        va_end(copy);
        if (line[0] != '\0') {
            log_instance_->pushLogLine(line);
        }
    }
    if (prev_log_ != nullptr) {
        return prev_log_(fmt, args);
    }
    return vprintf(fmt, args);
}

Status WebUiService::init() {
    mutex_ = xSemaphoreCreateMutex();
    if (mutex_ == nullptr) {
        return Error::NoMemory;
    }

    GALLUS_RETURN_IF_ERROR(
        rest_.registerRoute(HTTP_GET, "/", &pageHandler, this));
    GALLUS_RETURN_IF_ERROR(
        rest_.registerRoute(HTTP_GET, "/favicon.ico", &faviconHandler, this));
    GALLUS_RETURN_IF_ERROR(
        rest_.registerRoute(HTTP_GET, "/favicon.png", &faviconHandler, this));
    GALLUS_RETURN_IF_ERROR(rest_.registerWebSocket("/ws", &wsHandler, this));

    GALLUS_RETURN_IF_ERROR(
        events_.subscribe(EventId::BatteryChanged, &onEvent, this).status());
    GALLUS_RETURN_IF_ERROR(
        events_.subscribe(EventId::WiFiConnected, &onEvent, this).status());
    GALLUS_RETURN_IF_ERROR(
        events_.subscribe(EventId::ModuleStarted, &onEvent, this).status());
    GALLUS_RETURN_IF_ERROR(
        events_.subscribe(EventId::OTAProgress, &onEvent, this).status());
    GALLUS_RETURN_IF_ERROR(
        events_.subscribe(EventId::OTAFinished, &onEvent, this).status());

    log_instance_ = this;
    prev_log_ = esp_log_set_vprintf(&WebUiService::logVprintf);

    Log::info(kTag, "dashboard ready at / (ws at /ws)");
    return Status::success();
}

esp_err_t WebUiService::pageHandler(httpd_req_t* req) {
    const size_t len = kDashboardEnd - kDashboardStart;
    httpd_resp_set_type(req, "text/html");
    httpd_resp_set_hdr(req, "Content-Encoding", "gzip");
    return httpd_resp_send(req, reinterpret_cast<const char*>(kDashboardStart),
                           len);
}

esp_err_t WebUiService::faviconHandler(httpd_req_t* req) {
    const uint8_t* data = kFaviconIcoStart;
    size_t len = kFaviconIcoEnd - kFaviconIcoStart;
    const char* type = "image/x-icon";

    if (strcmp(req->uri, "/favicon.png") == 0) {
        data = kFaviconPngStart;
        len = kFaviconPngEnd - kFaviconPngStart;
        type = "image/png";
    }

    httpd_resp_set_type(req, type);
    httpd_resp_set_hdr(req, "Cache-Control", "public, max-age=86400");
    return httpd_resp_send(req, reinterpret_cast<const char*>(data), len);
}

esp_err_t WebUiService::wsHandler(httpd_req_t* req) {
    auto* self = static_cast<WebUiService*>(req->user_ctx);

    if (req->method == HTTP_GET) {
        self->addClient(httpd_req_to_sockfd(req));
        Log::info(kTag, "ws client connected (fd %d)",
                  httpd_req_to_sockfd(req));
        return ESP_OK;
    }

    const int fd = httpd_req_to_sockfd(req);
    httpd_ws_frame_t frame = {};
    if (httpd_ws_recv_frame(req, &frame, 0) != ESP_OK) {
        self->removeClient(fd);
        return ESP_FAIL;
    }
    if (frame.type == HTTPD_WS_TYPE_CLOSE) {
        self->removeClient(fd);
        Log::info(kTag, "ws client closed (fd %d)", fd);
        return ESP_FAIL;
    }
    if (frame.len > 0) {
        uint8_t sink[128];
        if (frame.len > sizeof(sink)) {
            self->removeClient(fd);
            return ESP_FAIL;
        }
        frame.payload = sink;
        if (httpd_ws_recv_frame(req, &frame, frame.len) != ESP_OK) {
            self->removeClient(fd);
            return ESP_FAIL;
        }
    }
    return ESP_OK;
}

void WebUiService::addClient(int fd) {
    xSemaphoreTake(mutex_, portMAX_DELAY);
    for (size_t i = 0; i < client_count_; ++i) {
        if (clients_[i] == fd) {
            xSemaphoreGive(mutex_);
            return;
        }
    }
    if (client_count_ < kMaxClients) {
        clients_[client_count_++] = fd;
    }
    xSemaphoreGive(mutex_);
}

void WebUiService::removeClient(int fd) {
    xSemaphoreTake(mutex_, portMAX_DELAY);
    for (size_t i = 0; i < client_count_; ++i) {
        if (clients_[i] == fd) {
            clients_[i] = clients_[--client_count_];
            break;
        }
    }
    xSemaphoreGive(mutex_);
}

void WebUiService::onEvent(const Event& event, void* ctx) {
    auto* self = static_cast<WebUiService*>(ctx);
    char json[192];

    switch (event.id) {
        case EventId::BatteryChanged: {
            const auto* p = event.as<BatteryService::ChangedPayload>();
            if (p == nullptr) return;
            snprintf(json, sizeof(json),
                     "{\"type\":\"battery\",\"mv\":%u,\"pct\":%u}",
                     p->millivolts, p->percent);
            break;
        }
        case EventId::WiFiConnected: {
            const auto* p = event.as<WifiService::ConnectedPayload>();
            if (p == nullptr) return;
            snprintf(json, sizeof(json),
                     "{\"type\":\"wifi\",\"connected\":true,"
                     "\"ip\":\"%u.%u.%u.%u\"}",
                     p->ip[0], p->ip[1], p->ip[2], p->ip[3]);
            break;
        }
        case EventId::ModuleStarted: {
            const auto* p = event.as<ModulePayload>();
            if (p == nullptr) return;
            snprintf(json, sizeof(json),
                     "{\"type\":\"module\",\"name\":\"%s\","
                     "\"state\":\"started\"}",
                     p->name);
            break;
        }
        case EventId::OTAProgress: {
            const auto* p = event.as<OtaService::ProgressPayload>();
            if (p == nullptr) return;
            snprintf(json, sizeof(json),
                     "{\"type\":\"ota\",\"percent\":%u}", p->percent);
            break;
        }
        case EventId::OTAFinished:
            snprintf(json, sizeof(json), "{\"type\":\"ota\",\"done\":true}");
            break;
        default:
            return;
    }
    self->broadcast(json);
}

void WebUiService::broadcast(const char* json) {
    httpd_handle_t server = rest_.server();
    if (server == nullptr) {
        return;
    }

    xSemaphoreTake(mutex_, portMAX_DELAY);
    for (size_t i = 0; i < client_count_; ++i) {
        auto* work = static_cast<WsSend*>(malloc(sizeof(WsSend)));
        if (work == nullptr) {
            continue;
        }
        work->service = this;
        work->server = server;
        work->fd = clients_[i];
        work->len = strnlen(json, sizeof(work->data) - 1);
        memcpy(work->data, json, work->len);
        work->data[work->len] = '\0';
        if (httpd_queue_work(server, &wsSendWorker, work) != ESP_OK) {
            free(work);
        }
    }
    xSemaphoreGive(mutex_);
}

void WebUiService::pushLogLine(const char* line) {
    char escaped[240] = {};
    size_t j = 0;
    for (const char* p = line; *p != '\0' && j + 2 < sizeof(escaped); ++p) {
        if (*p == '"' || *p == '\\') {
            if (j + 1 >= sizeof(escaped)) {
                break;
            }
            escaped[j++] = '\\';
        }
        if (*p == '\n' || *p == '\r') {
            continue;
        }
        escaped[j++] = *p;
    }
    escaped[j] = '\0';

    char json[280] = {};
    snprintf(json, sizeof(json), "{\"type\":\"log\",\"line\":\"%s\"}",
             escaped);
    broadcast(json);
}

}  // namespace gallus::services
