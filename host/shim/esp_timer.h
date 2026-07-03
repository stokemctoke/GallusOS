#pragma once

#include <cstdint>

#include "esp_err.h"

using esp_timer_handle_t = void*;

typedef void (*esp_timer_cb_t)(void* arg);

typedef enum {
    ESP_TIMER_TASK,
} esp_timer_dispatch_t;

struct esp_timer_create_args_t {
    esp_timer_cb_t callback;
    void* arg;
    esp_timer_dispatch_t dispatch_method;
    const char* name;
    bool skip_unhandled_events;
};

int64_t esp_timer_get_time();
esp_err_t esp_timer_create(const esp_timer_create_args_t* args,
                           esp_timer_handle_t* out_handle);
esp_err_t esp_timer_start_once(esp_timer_handle_t handle, uint64_t timeout_us);
esp_err_t esp_timer_start_periodic(esp_timer_handle_t handle,
                                   uint64_t period_us);
esp_err_t esp_timer_stop(esp_timer_handle_t handle);
esp_err_t esp_timer_delete(esp_timer_handle_t handle);
