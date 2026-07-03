#pragma once

#include <cassert>
#include <cstdint>
#include <new>
#include <type_traits>
#include <utility>

#include "esp_err.h"

/// @file error.hpp
/// @brief The GallusOS error-handling convention.
///
/// Exceptions and RTTI are disabled. Every fallible operation returns
/// gallus::Status (void results) or gallus::Result<T> (value results),
/// both wrapping gallus::Error. Interop helpers convert to and from
/// esp_err_t at the ESP-IDF boundary.

namespace gallus {

/// Framework-wide error codes.
enum class Error : uint8_t {
    None = 0,        ///< Success.
    Timeout,         ///< Operation timed out.
    NoMemory,        ///< Allocation or capacity exhausted.
    InvalidArg,      ///< Caller passed a bad argument.
    InvalidState,    ///< Object not in a state that allows the call.
    NotFound,        ///< Requested item does not exist.
    NotSupported,    ///< Operation unsupported on this platform/build.
    Busy,            ///< Resource temporarily unavailable.
    QueueFull,       ///< Bounded queue rejected an item.
    PermissionDenied,///< Reserved/protected resource.
    Hardware,        ///< Underlying hardware fault.
    Internal,        ///< Unexpected framework failure.
};

/// @return Human-readable name for @p err (never nullptr).
const char* toString(Error err);

/// Map an esp_err_t to the nearest gallus::Error.
Error fromEspErr(esp_err_t err);

/// Map a gallus::Error to the nearest esp_err_t.
esp_err_t toEspErr(Error err);

/// Result of a fallible operation that returns no value.
class [[nodiscard]] Status {
public:
    constexpr Status() : err_(Error::None) {}
    constexpr Status(Error err) : err_(err) {}  // NOLINT: implicit by design

    /// Success factory, for readability at return sites.
    static constexpr Status success() { return Status(); }
    /// Failure factory.
    static constexpr Status failure(Error err) { return Status(err); }

    [[nodiscard]] constexpr bool ok() const { return err_ == Error::None; }
    [[nodiscard]] constexpr Error error() const { return err_; }
    [[nodiscard]] const char* message() const { return toString(err_); }

    constexpr explicit operator bool() const { return ok(); }

private:
    Error err_;
};

/// Result of a fallible operation that produces a value of type T.
///
/// T must be movable. The value is only accessible when ok() is true;
/// accessing value() on a failed Result is a programming error and
/// asserts in debug builds.
template <typename T>
class [[nodiscard]] Result {
    static_assert(!std::is_reference_v<T>, "Result<T&> is not supported");

public:
    Result(T value) : has_value_(true) {  // NOLINT: implicit by design
        new (&storage_) T(std::move(value));
    }

    Result(Error err) : err_(err), has_value_(false) {}  // NOLINT

    Result(Result&& other) noexcept(std::is_nothrow_move_constructible_v<T>)
        : err_(other.err_), has_value_(other.has_value_) {
        if (has_value_) {
            new (&storage_) T(std::move(other.valueRef()));
        }
    }

    Result& operator=(Result&& other) noexcept(
        std::is_nothrow_move_constructible_v<T>) {
        if (this != &other) {
            destroy();
            err_ = other.err_;
            has_value_ = other.has_value_;
            if (has_value_) {
                new (&storage_) T(std::move(other.valueRef()));
            }
        }
        return *this;
    }

    Result(const Result&) = delete;
    Result& operator=(const Result&) = delete;

    ~Result() { destroy(); }

    [[nodiscard]] bool ok() const { return has_value_; }
    explicit operator bool() const { return ok(); }

    /// Error code (Error::None when ok()).
    [[nodiscard]] Error error() const { return has_value_ ? Error::None : err_; }
    [[nodiscard]] const char* message() const { return toString(error()); }

    /// Access the contained value. Only valid when ok().
    [[nodiscard]] T& value() & {
        assert(has_value_);
        return valueRef();
    }
    [[nodiscard]] const T& value() const& {
        assert(has_value_);
        return valueRef();
    }
    [[nodiscard]] T&& value() && {
        assert(has_value_);
        return std::move(valueRef());
    }

    /// The value, or @p fallback when the operation failed.
    [[nodiscard]] T valueOr(T fallback) const& {
        return has_value_ ? valueRef() : std::move(fallback);
    }

    /// The Status equivalent of this result.
    [[nodiscard]] Status status() const { return Status(error()); }

private:
    T& valueRef() { return *std::launder(reinterpret_cast<T*>(&storage_)); }
    const T& valueRef() const {
        return *std::launder(reinterpret_cast<const T*>(&storage_));
    }

    void destroy() {
        if (has_value_) {
            valueRef().~T();
            has_value_ = false;
        }
    }

    alignas(T) unsigned char storage_[sizeof(T)];
    Error err_ = Error::None;
    bool has_value_;
};

/// Propagate a failed Status/Result from the current function.
/// Usage: GALLUS_RETURN_IF_ERROR(bus.publish(evt));
#define GALLUS_RETURN_IF_ERROR(expr)                        \
    do {                                                    \
        const auto gallus_status_ = (expr);                 \
        if (!gallus_status_.ok()) {                         \
            return gallus_status_.error();                  \
        }                                                   \
    } while (0)

}  // namespace gallus
