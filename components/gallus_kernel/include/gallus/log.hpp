#pragma once

/// @file log.hpp
/// @brief Kernel logging facade.
///
/// Wraps ESP-IDF logging today (serial backend). Future backends
/// (file, web console over WebSocket) plug in behind this facade
/// without touching call sites.

namespace gallus {

class Log {
public:
    enum class Level {
        None,
        Error,
        Warn,
        Info,
        Debug,
        Verbose,
    };

    /// Set the maximum level emitted for @p tag ("*" for all tags).
    static void setLevel(const char* tag, Level level);

    [[gnu::format(printf, 2, 3)]]
    static void error(const char* tag, const char* fmt, ...);

    [[gnu::format(printf, 2, 3)]]
    static void warn(const char* tag, const char* fmt, ...);

    [[gnu::format(printf, 2, 3)]]
    static void info(const char* tag, const char* fmt, ...);

    [[gnu::format(printf, 2, 3)]]
    static void debug(const char* tag, const char* fmt, ...);

    [[gnu::format(printf, 2, 3)]]
    static void verbose(const char* tag, const char* fmt, ...);
};

}  // namespace gallus
