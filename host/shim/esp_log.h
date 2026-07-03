#pragma once

#include <cstdarg>
#include <cstdint>

#define LOG_COLOR_E ""
#define LOG_COLOR_W ""
#define LOG_COLOR_I ""
#define LOG_RESET_COLOR ""

typedef enum {
    ESP_LOG_NONE,
    ESP_LOG_ERROR,
    ESP_LOG_WARN,
    ESP_LOG_INFO,
    ESP_LOG_DEBUG,
    ESP_LOG_VERBOSE,
} esp_log_level_t;

void esp_log_level_set(const char* tag, esp_log_level_t level);
esp_log_level_t esp_log_level_get(const char* tag);
uint32_t esp_log_timestamp();
void esp_log_write(esp_log_level_t level, const char* tag, const char* fmt, ...);
void esp_log_writev(esp_log_level_t level, const char* tag, const char* fmt,
                    va_list args);
