#include "gallus/services/ieee802154_service.hpp"

#include <cstring>

#include "esp_ieee802154.h"
#include "esp_ieee802154_types.h"
#include "freertos/task.h"

#include "gallus/log.hpp"

namespace gallus::services {

namespace {

constexpr const char* kTag = "802154";

// The one survey in flight; the driver's frame/energy callbacks are
// weak C globals with no context, so the active service is parked here.
// Guarded by Ieee802154Service::scanning_.
Ieee802154Service* s_active = nullptr;

/// Add @p value to @p list (length @p count, capacity @p cap) if absent.
template <typename T>
void addUnique(T* list, uint8_t& count, uint8_t cap, T value) {
    for (uint8_t i = 0; i < count; ++i) {
        if (list[i] == value) {
            return;
        }
    }
    if (count < cap) {
        list[count++] = value;
    }
}

}  // namespace

void Ieee802154Service::recordFrame(const uint8_t* frame, int8_t rssi) {
    ChannelRecord* rec = current_;
    if (rec == nullptr || frame == nullptr) {
        return;
    }

    // frame[0] is the PHY length (PSDU incl. the 2-byte FCS, which the
    // driver replaces with RSSI/LQI). The MAC frame starts at frame[1].
    const uint8_t psdu_len = frame[0];
    if (psdu_len < 3) {
        return;  // too short for an FCF
    }
    const uint8_t* mac = frame + 1;
    const uint8_t mac_len = psdu_len;  // includes trailing 2 bytes

    rec->frames++;
    if (rssi > rec->best_rssi) {
        rec->best_rssi = rssi;
    }

    const uint16_t fcf = static_cast<uint16_t>(mac[0] | (mac[1] << 8));
    const uint8_t ftype = fcf & 0x0007;
    const uint8_t dest_mode = (fcf >> 10) & 0x0003;
    const uint8_t src_mode = (fcf >> 14) & 0x0003;
    const bool pan_compress = (fcf >> 6) & 0x0001;

    switch (ftype) {
        case 0: rec->frame_types |= kTypeBeacon; break;
        case 1: rec->frame_types |= kTypeData; break;
        case 2: rec->frame_types |= kTypeAck; break;
        case 3: rec->frame_types |= kTypeCommand; break;
        default: break;
    }

    // Walk the addressing fields, bounds-checked against mac_len - 2
    // (the last two bytes are RSSI/LQI, not header).
    size_t off = 3;  // FCF (2) + sequence number (1)
    const size_t limit = mac_len >= 2 ? mac_len - 2 : 0;
    auto avail = [&](size_t n) { return off + n <= limit; };

    uint16_t dest_pan = 0xFFFF;
    if (dest_mode != 0) {
        if (!avail(2)) return;
        dest_pan = static_cast<uint16_t>(mac[off] | (mac[off + 1] << 8));
        off += 2;
        const size_t alen = (dest_mode == 2) ? 2 : 8;
        if (!avail(alen)) return;
        off += alen;
    }

    uint16_t src_pan = 0xFFFF;
    if (src_mode != 0) {
        if (!pan_compress) {
            if (!avail(2)) return;
            src_pan = static_cast<uint16_t>(mac[off] | (mac[off + 1] << 8));
            off += 2;
        } else {
            src_pan = dest_pan;
        }
        if (src_mode == 2) {
            if (!avail(2)) return;
            const uint16_t saddr =
                static_cast<uint16_t>(mac[off] | (mac[off + 1] << 8));
            addUnique<uint16_t>(rec->devices, rec->device_count, 6, saddr);
        }
    }

    const uint16_t pan = dest_pan != 0xFFFF ? dest_pan : src_pan;
    if (pan != 0xFFFF && pan != 0x0000) {
        addUnique<uint16_t>(rec->pans, rec->pan_count, 4, pan);
    }
}

void Ieee802154Service::onReceive(const uint8_t* frame, const void* info_v) {
    if (s_active != nullptr) {
        const auto* info =
            static_cast<const esp_ieee802154_frame_info_t*>(info_v);
        const int8_t rssi = info != nullptr ? info->rssi : -128;
        s_active->recordFrame(frame, rssi);
    }
}

void Ieee802154Service::onEnergyDone(int8_t power) {
    Ieee802154Service* self = s_active;
    if (self == nullptr) {
        return;
    }
    self->energy_result_ = power;
    if (self->energy_sem_ != nullptr) {
        BaseType_t woken = pdFALSE;
        xSemaphoreGiveFromISR(self->energy_sem_, &woken);
        portYIELD_FROM_ISR(woken);
    }
}

Result<size_t> Ieee802154Service::scan(ChannelRecord* out, size_t max,
                                       uint32_t /*dwell_ms*/) {
    if (out == nullptr || max == 0) {
        return Error::InvalidArg;
    }

    bool expected = false;
    if (!scanning_.compare_exchange_strong(expected, true)) {
        return Error::Busy;
    }

    energy_sem_ = xSemaphoreCreateBinary();
    if (energy_sem_ == nullptr) {
        scanning_.store(false, std::memory_order_relaxed);
        return Error::NoMemory;
    }

    esp_err_t err = esp_ieee802154_enable();
    if (err != ESP_OK) {
        vSemaphoreDelete(energy_sem_);
        energy_sem_ = nullptr;
        scanning_.store(false, std::memory_order_relaxed);
        Log::error(kTag, "enable failed: %s", esp_err_to_name(err));
        return fromEspErr(err);
    }

    s_active = this;

    // Energy scan only: sample each channel a few times (peak-hold) with
    // sub-millisecond energy detects. No promiscuous receive and no long
    // per-channel dwell, so the shared 2.4 GHz radio is never parked away
    // from WiFi long enough to drop the dashboard connection.
    size_t written = 0;
    for (uint8_t ch = kChannelMin; ch <= kChannelMax && written < max; ++ch) {
        ChannelRecord& rec = out[written];
        rec = ChannelRecord{};
        rec.channel = ch;
        rec.energy_dbm = -128;
        rec.best_rssi = -128;

        (void)esp_ieee802154_set_channel(ch);

        int8_t peak = -128;
        for (int s = 0; s < kEnergySamples; ++s) {
            energy_result_ = -128;
            if (esp_ieee802154_energy_detect(kEnergyDurationSymbols) ==
                ESP_OK) {
                if (xSemaphoreTake(energy_sem_, pdMS_TO_TICKS(15)) == pdTRUE) {
                    if (energy_result_ > peak) {
                        peak = energy_result_;
                    }
                }
            }
        }
        rec.energy_dbm = peak;
        ++written;
    }

    s_active = nullptr;
    (void)esp_ieee802154_disable();
    vSemaphoreDelete(energy_sem_);
    energy_sem_ = nullptr;
    scanning_.store(false, std::memory_order_relaxed);

    Log::info(kTag, "energy survey complete: %u channel(s)",
              static_cast<unsigned>(written));
    return written;
}

}  // namespace gallus::services

// ---------------------------------------------------------------------------
// Driver callbacks (weak symbols in the IDF driver; our definitions win).
// These run in the 802.15.4 driver context.
// ---------------------------------------------------------------------------

extern "C" void esp_ieee802154_receive_done(
    uint8_t* frame, esp_ieee802154_frame_info_t* frame_info) {
    gallus::services::Ieee802154Service::onReceive(frame, frame_info);
    // Release the driver's frame buffer.
    esp_ieee802154_receive_handle_done(frame);
}

extern "C" void esp_ieee802154_energy_detect_done(int8_t power) {
    gallus::services::Ieee802154Service::onEnergyDone(power);
}
