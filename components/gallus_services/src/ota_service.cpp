#include "gallus/services/ota_service.hpp"

#include <cstdlib>
#include <cstring>

#include "esp_ota_ops.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "gallus/kernel.hpp"
#include "gallus/log.hpp"

namespace gallus::services {

namespace {

constexpr const char* kTag = "OTA";
constexpr size_t kChunkBytes = 1024;

void rebootJob(void* /*ctx*/) { esp_restart(); }

}  // namespace

Status OtaService::init() {
    return rest_.registerRoute(HTTP_POST, "/api/v1/ota/upload",
                               &OtaService::uploadHandler, this);
}

void OtaService::confirmHealthy() {
    const esp_partition_t* running = esp_ota_get_running_partition();
    esp_ota_img_states_t state;
    if (esp_ota_get_state_partition(running, &state) != ESP_OK) {
        return;
    }
    if (state == ESP_OTA_IMG_PENDING_VERIFY) {
        if (esp_ota_mark_app_valid_cancel_rollback() == ESP_OK) {
            Log::info(kTag, "running image marked valid (rollback cancelled)");
        }
    }
}

esp_err_t OtaService::uploadHandler(httpd_req_t* req) {
    auto* self = static_cast<OtaService*>(req->user_ctx);
    if (!self->rest_.authorize(req)) {
        return ESP_OK;
    }

    if (self->in_progress_) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST,
                            "update already in progress");
        return ESP_FAIL;
    }

    const int total = req->content_len;
    if (total <= 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "empty firmware");
        return ESP_FAIL;
    }

    const esp_partition_t* target = esp_ota_get_next_update_partition(nullptr);
    if (target == nullptr) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR,
                            "no OTA partition");
        return ESP_FAIL;
    }

    self->in_progress_ = true;
    Log::info(kTag, "update starting: %d bytes -> %s", total, target->label);
    (void)self->events_.publish(Event::make(EventId::OTAStarted));

    esp_ota_handle_t handle = 0;
    esp_err_t err = esp_ota_begin(target, OTA_SIZE_UNKNOWN, &handle);
    if (err != ESP_OK) {
        self->in_progress_ = false;
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR,
                            "ota begin failed");
        return ESP_FAIL;
    }

    char* buf = static_cast<char*>(malloc(kChunkBytes));
    if (buf == nullptr) {
        esp_ota_abort(handle);
        self->in_progress_ = false;
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "no memory");
        return ESP_FAIL;
    }

    int received = 0;
    uint8_t last_pct = 255;
    bool failed = false;
    while (received < total) {
        const int want =
            static_cast<int>(total - received) < static_cast<int>(kChunkBytes)
                ? (total - received)
                : static_cast<int>(kChunkBytes);
        const int got = httpd_req_recv(req, buf, want);
        if (got <= 0) {
            if (got == HTTPD_SOCK_ERR_TIMEOUT) {
                continue;
            }
            failed = true;
            break;
        }

        if (esp_ota_write(handle, buf, got) != ESP_OK) {
            failed = true;
            break;
        }
        received += got;

        const uint8_t pct = static_cast<uint8_t>(
            static_cast<int64_t>(received) * 100 / total);
        if (pct != last_pct) {
            last_pct = pct;
            const ProgressPayload payload = {
                .received = static_cast<uint32_t>(received),
                .total = static_cast<uint32_t>(total),
                .percent = pct,
            };
            (void)self->events_.publish(
                Event::make(EventId::OTAProgress, payload));
        }
    }
    free(buf);

    if (failed) {
        esp_ota_abort(handle);
        self->in_progress_ = false;
        const FinishedPayload payload = {.success = 0};
        (void)self->events_.publish(
            Event::make(EventId::OTAFinished, payload));
        Log::error(kTag, "update failed after %d bytes", received);
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR,
                            "write failed");
        return ESP_FAIL;
    }

    err = esp_ota_end(handle);
    if (err != ESP_OK) {
        self->in_progress_ = false;
        const FinishedPayload payload = {.success = 0};
        (void)self->events_.publish(
            Event::make(EventId::OTAFinished, payload));
        Log::error(kTag, "image validation failed: %s", esp_err_to_name(err));
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST,
                            "invalid firmware image");
        return ESP_FAIL;
    }

    err = esp_ota_set_boot_partition(target);
    if (err != ESP_OK) {
        self->in_progress_ = false;
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR,
                            "set boot partition failed");
        return ESP_FAIL;
    }

    const FinishedPayload payload = {.success = 1};
    (void)self->events_.publish(Event::make(EventId::OTAFinished, payload));
    Log::info(kTag, "update complete — rebooting into %s", target->label);

    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, "{\"status\":\"ok\",\"rebooting\":true}");

    // Give the response time to flush, then reboot from a job.
    (void)Kernel::instance().scheduler().once(1500, &rebootJob, nullptr,
                                              Priority::Normal);
    return ESP_OK;
}

}  // namespace gallus::services
