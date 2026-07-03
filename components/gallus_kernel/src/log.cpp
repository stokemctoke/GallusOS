#include "gallus/log.hpp"

#include "esp_log.h"

namespace gallus {

namespace {

esp_log_level_t toEsp(Log::Level level) {
    switch (level) {
        case Log::Level::None:    return ESP_LOG_NONE;
        case Log::Level::Error:   return ESP_LOG_ERROR;
        case Log::Level::Warn:    return ESP_LOG_WARN;
        case Log::Level::Info:    return ESP_LOG_INFO;
        case Log::Level::Debug:   return ESP_LOG_DEBUG;
        case Log::Level::Verbose: return ESP_LOG_VERBOSE;
    }
    return ESP_LOG_INFO;
}

}  // namespace

void Log::setLevel(const char* tag, Level level) {
    esp_log_level_set(tag, toEsp(level));
}

// Each level emits the standard IDF-style prefix, the user message and a
// newline through esp_log_write*, so output routes through whatever
// backend esp_log has installed (serial today, pluggable later).
#define GALLUS_LOG_IMPL(name, level, letter, color)                       \
    void Log::name(const char* tag, const char* fmt, ...) {               \
        const esp_log_level_t esp_level = toEsp(level);                   \
        if (esp_log_level_get(tag) < esp_level) {                         \
            return;                                                       \
        }                                                                 \
        esp_log_write(esp_level, tag, color letter " (%u) %s: ",          \
                      (unsigned)esp_log_timestamp(), tag);                \
        va_list args;                                                     \
        va_start(args, fmt);                                              \
        esp_log_writev(esp_level, tag, fmt, args);                        \
        va_end(args);                                                     \
        esp_log_write(esp_level, tag, LOG_RESET_COLOR "\n");              \
    }

GALLUS_LOG_IMPL(error, Level::Error, "E", LOG_COLOR_E)
GALLUS_LOG_IMPL(warn, Level::Warn, "W", LOG_COLOR_W)
GALLUS_LOG_IMPL(info, Level::Info, "I", LOG_COLOR_I)
GALLUS_LOG_IMPL(debug, Level::Debug, "D", "")
GALLUS_LOG_IMPL(verbose, Level::Verbose, "V", "")

#undef GALLUS_LOG_IMPL

}  // namespace gallus
