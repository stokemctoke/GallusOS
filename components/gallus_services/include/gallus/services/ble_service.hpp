#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>

#include "gallus/error.hpp"

#ifndef GALLUS_HOST
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

// Forward-declare the NimBLE event struct at GLOBAL scope so the member
// signatures below bind to ::ble_gap_event (defined by host/ble_gap.h in
// the .cpp) rather than implicitly declaring a namespace-local type.
struct ble_gap_event;
#endif

/// @file ble_service.hpp
/// @brief On-demand BLE scanner (NimBLE host).
///
/// The ESP32-C5 supports Bluetooth Low Energy 5.0 (no Bluetooth
/// Classic). To keep the radio stack out of the steady-state heap
/// budget, the NimBLE host is brought up only for the duration of a
/// scan() call and torn down afterwards — so BLE costs RAM only while
/// actively scanning, never at rest. BLE coexists with WiFi via the
/// coexistence arbiter, so a scan does not drop the STA connection.

namespace gallus::services {

class BleService {
public:
    /// One BLE device seen during a scan (deduplicated by address).
    struct BleRecord {
        uint8_t addr[6];        ///< Device address (big-endian display order).
        uint8_t addr_type;      ///< 0 public, 1 random, 2 public-id, 3 random-id.
        int8_t rssi;
        int8_t tx_power;        ///< Valid only when kFlagHasTxPower is set.
        uint16_t company_id;    ///< Manufacturer company ID (0xFFFF = none).
        uint16_t services[4];   ///< Up to 4 advertised 16-bit service UUIDs.
        uint8_t service_count;
        uint8_t flags;          ///< See kFlag* below.
        char name[32];          ///< Advertised local name ("" if none).
    };

    static constexpr uint8_t kFlagConnectable = 1 << 0;
    static constexpr uint8_t kFlagHasTxPower = 1 << 1;
    static constexpr uint8_t kFlagNameComplete = 1 << 2;

    static constexpr size_t kMaxScanResults = 32;
    static constexpr uint32_t kDefaultScanMs = 3000;
    static constexpr uint32_t kMaxScanMs = 10000;

    BleService() = default;
    BleService(const BleService&) = delete;
    BleService& operator=(const BleService&) = delete;

    /// Blocking scan: bring NimBLE up, collect advertisements for
    /// @p duration_ms, tear it down, and return the device count.
    /// Writes up to @p max records to @p out. Busy if a scan is already
    /// running. @p duration_ms is clamped to kMaxScanMs.
    Result<size_t> scan(BleRecord* out, size_t max,
                        uint32_t duration_ms = kDefaultScanMs);

    /// True while a scan is in progress (the stack is up).
    [[nodiscard]] bool scanning() const {
        return scanning_.load(std::memory_order_relaxed);
    }

private:
    std::atomic<bool> scanning_{false};

#ifndef GALLUS_HOST
    static void onSync();
    static void hostTask(void* param);
    static int gapEvent(struct ble_gap_event* event, void* arg);
    void recordAdvert(const struct ble_gap_event* event);

    // Scan state, valid only for the duration of one scan() call.
    BleRecord* out_ = nullptr;
    size_t max_ = 0;
    size_t count_ = 0;
    uint32_t duration_ms_ = kDefaultScanMs;
    SemaphoreHandle_t done_sem_ = nullptr;
#endif
};

}  // namespace gallus::services
