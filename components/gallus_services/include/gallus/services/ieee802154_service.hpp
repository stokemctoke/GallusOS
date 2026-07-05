#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>

#include "gallus/error.hpp"

#ifndef GALLUS_HOST
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#endif

/// @file ieee802154_service.hpp
/// @brief On-demand IEEE 802.15.4 channel + network survey.
///
/// 802.15.4 is the radio under Zigbee, Thread and Matter-over-Thread —
/// an IoT ecosystem invisible to WiFi and BLE scans. The ESP32-C5 has
/// the radio; this service passively surveys the 16 channels (11–26):
/// an energy scan measures activity per channel, and a brief
/// promiscuous capture enumerates the networks (PAN IDs) and device
/// addresses heard. Payloads are network-key encrypted, so this reports
/// presence and metadata, not decoded traffic.
///
/// Like BleService, the radio is brought up only for the scan and torn
/// down after, so it costs RAM only while scanning. It coexists with
/// WiFi, so the dashboard connection is preserved.

namespace gallus::services {

class Ieee802154Service {
public:
    /// One 802.15.4 channel's survey result.
    struct ChannelRecord {
        uint8_t channel;        ///< 11–26.
        int8_t energy_dbm;      ///< Peak energy (-128 = not measured).
        int8_t best_rssi;       ///< Strongest captured frame (-128 = none).
        uint16_t frames;        ///< Frames captured during the dwell.
        uint16_t pans[4];       ///< Distinct PAN IDs heard.
        uint8_t pan_count;
        uint16_t devices[6];    ///< Distinct 16-bit device addresses heard.
        uint8_t device_count;
        uint8_t frame_types;    ///< bit0 beacon,1 data,2 ack,3 command.
    };

    static constexpr uint8_t kChannelMin = 11;
    static constexpr uint8_t kChannelMax = 26;
    static constexpr size_t kChannelCount = 16;

    // Energy-scan mode: a handful of quick (~128 us) energy-detect
    // samples per channel, peak-held. The radio is tuned away from WiFi
    // only microseconds at a time, so the survey coexists with the
    // dashboard connection (unlike a long promiscuous dwell, which
    // monopolises the shared 2.4 GHz radio and breaks WiFi).
    static constexpr int kEnergySamples = 6;
    static constexpr uint32_t kEnergyDurationSymbols = 8;  // ~128 us

    // Reserved for a future opt-in "deep capture" mode that enumerates
    // PAN IDs / devices via promiscuous frame capture (disrupts WiFi).
    static constexpr uint32_t kDefaultDwellMs = 0;
    static constexpr uint32_t kMaxDwellMs = 500;

    static constexpr uint8_t kTypeBeacon = 1 << 0;
    static constexpr uint8_t kTypeData = 1 << 1;
    static constexpr uint8_t kTypeAck = 1 << 2;
    static constexpr uint8_t kTypeCommand = 1 << 3;

    Ieee802154Service() = default;
    Ieee802154Service(const Ieee802154Service&) = delete;
    Ieee802154Service& operator=(const Ieee802154Service&) = delete;

    /// Blocking survey: bring the radio up, sweep channels 11–26 with an
    /// energy scan plus a @p dwell_ms promiscuous capture each, tear it
    /// down, and return the number of channel records written (up to 16,
    /// capped by @p max). Busy if a survey is already running.
    Result<size_t> scan(ChannelRecord* out, size_t max,
                        uint32_t dwell_ms = kDefaultDwellMs);

    [[nodiscard]] bool scanning() const {
        return scanning_.load(std::memory_order_relaxed);
    }

#ifndef GALLUS_HOST
    // Invoked from the driver's weak C callbacks (see the .cpp). @p info
    // is a const esp_ieee802154_frame_info_t* passed as void* to keep
    // this header free of the driver type (an anonymous-struct typedef).
    static void onReceive(const uint8_t* frame, const void* info);
    static void onEnergyDone(int8_t power);
#endif

private:
    std::atomic<bool> scanning_{false};

#ifndef GALLUS_HOST
    void recordFrame(const uint8_t* frame, int8_t rssi);

    ChannelRecord* current_ = nullptr;   ///< Record being filled this channel.
    SemaphoreHandle_t energy_sem_ = nullptr;
    volatile int8_t energy_result_ = -128;
#endif
};

}  // namespace gallus::services
