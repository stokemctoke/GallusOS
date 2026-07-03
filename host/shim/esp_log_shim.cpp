#include "esp_log.h"

#include <chrono>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>
#include <unordered_map>

namespace {

constexpr esp_log_level_t kDefaultLevel = ESP_LOG_INFO;
std::mutex g_mutex;
std::unordered_map<std::string, esp_log_level_t> g_levels;

esp_log_level_t levelFor(const char* tag) {
    if (tag == nullptr) {
        return kDefaultLevel;
    }
    const auto it = g_levels.find(tag);
    return it == g_levels.end() ? kDefaultLevel : it->second;
}

const char* levelLetter(esp_log_level_t level) {
    switch (level) {
        case ESP_LOG_ERROR: return "E";
        case ESP_LOG_WARN: return "W";
        case ESP_LOG_INFO: return "I";
        case ESP_LOG_DEBUG: return "D";
        case ESP_LOG_VERBOSE: return "V";
        default: return "?";
    }
}

}  // namespace

void esp_log_level_set(const char* tag, esp_log_level_t level) {
    if (tag == nullptr) {
        return;
    }
    std::lock_guard<std::mutex> lock(g_mutex);
    g_levels[tag] = level;
}

esp_log_level_t esp_log_level_get(const char* tag) {
    std::lock_guard<std::mutex> lock(g_mutex);
    return levelFor(tag);
}

uint32_t esp_log_timestamp() {
    using clock = std::chrono::steady_clock;
    static const auto start = clock::now();
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                        clock::now() - start)
                        .count();
    return static_cast<uint32_t>(ms);
}

void esp_log_writev(esp_log_level_t level, const char* tag, const char* fmt,
                    va_list args) {
    if (levelFor(tag) < level) {
        return;
    }
    std::fprintf(stderr, "%s (%u) %s: ", levelLetter(level),
                 static_cast<unsigned>(esp_log_timestamp()),
                 tag != nullptr ? tag : "?");
    std::vfprintf(stderr, fmt, args);
}

void esp_log_write(esp_log_level_t level, const char* tag, const char* fmt,
                   ...) {
    va_list args;
    va_start(args, fmt);
    esp_log_writev(level, tag, fmt, args);
    va_end(args);
}
