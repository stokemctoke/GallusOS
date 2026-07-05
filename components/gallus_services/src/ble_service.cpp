#include "gallus/services/ble_service.hpp"

#include <cstring>

#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "host/util/util.h"

#include "gallus/log.hpp"

namespace gallus::services {

namespace {

constexpr const char* kTag = "BLE";

// The one scan in flight; NimBLE's C callbacks carry no context of
// their own (sync_cb in particular), so the active service is parked
// here for the duration of a scan. Guarded by BleService::scanning_.
BleService* s_active = nullptr;

/// Reverse NimBLE's little-endian address into display order (MSB first).
void addrToDisplay(const uint8_t* le, uint8_t* out) {
    for (int i = 0; i < 6; ++i) {
        out[i] = le[5 - i];
    }
}

}  // namespace

void BleService::recordAdvert(const struct ble_gap_event* event) {
    if (out_ == nullptr || count_ >= max_) {
        return;
    }

    uint8_t addr[6];
    addrToDisplay(event->disc.addr.val, addr);

    // Deduplicate: a device advertises repeatedly, and its scan
    // response arrives as a separate report. Merge into the existing
    // row so we keep the richest data (name often comes in the
    // response, not the initial advert).
    BleRecord* rec = nullptr;
    for (size_t i = 0; i < count_; ++i) {
        if (std::memcmp(out_[i].addr, addr, 6) == 0) {
            rec = &out_[i];
            break;
        }
    }
    if (rec == nullptr) {
        rec = &out_[count_++];
        *rec = BleRecord{};
        std::memcpy(rec->addr, addr, 6);
        rec->addr_type = event->disc.addr.type;
        rec->company_id = 0xFFFF;
    }
    rec->rssi = event->disc.rssi;

    switch (event->disc.event_type) {
        case BLE_HCI_ADV_RPT_EVTYPE_ADV_IND:
        case BLE_HCI_ADV_RPT_EVTYPE_DIR_IND:
            rec->flags |= kFlagConnectable;
            break;
        default:
            break;
    }

    struct ble_hs_adv_fields fields;
    if (ble_hs_adv_parse_fields(&fields, event->disc.data,
                                event->disc.length_data) != 0) {
        return;
    }

    if (fields.name != nullptr && fields.name_len > 0) {
        const size_t n = fields.name_len < sizeof(rec->name) - 1
                             ? fields.name_len
                             : sizeof(rec->name) - 1;
        std::memcpy(rec->name, fields.name, n);
        rec->name[n] = '\0';
        if (fields.name_is_complete) {
            rec->flags |= kFlagNameComplete;
        }
    }

    if (fields.tx_pwr_lvl_is_present) {
        rec->tx_power = fields.tx_pwr_lvl;
        rec->flags |= kFlagHasTxPower;
    }

    // Manufacturer data: first two bytes are the company identifier
    // (little-endian), per the Bluetooth assigned-numbers convention.
    if (fields.mfg_data != nullptr && fields.mfg_data_len >= 2) {
        rec->company_id = static_cast<uint16_t>(
            fields.mfg_data[0] | (fields.mfg_data[1] << 8));
    }

    for (int i = 0; i < fields.num_uuids16 &&
                    rec->service_count < 4;
         ++i) {
        rec->services[rec->service_count++] =
            ble_uuid_u16(&fields.uuids16[i].u);
    }
}

int BleService::gapEvent(struct ble_gap_event* event, void* /*arg*/) {
    BleService* self = s_active;
    if (self == nullptr) {
        return 0;
    }
    switch (event->type) {
        case BLE_GAP_EVENT_DISC:
            self->recordAdvert(event);
            break;
        case BLE_GAP_EVENT_DISC_COMPLETE:
            if (self->done_sem_ != nullptr) {
                xSemaphoreGive(self->done_sem_);
            }
            break;
        default:
            break;
    }
    return 0;
}

void BleService::onSync() {
    BleService* self = s_active;
    if (self == nullptr) {
        return;
    }

    // Make sure we have an identity address before scanning.
    if (ble_hs_util_ensure_addr(0) != 0) {
        Log::error(kTag, "no BLE address available");
        if (self->done_sem_ != nullptr) {
            xSemaphoreGive(self->done_sem_);
        }
        return;
    }

    uint8_t own_addr_type = 0;
    if (ble_hs_id_infer_auto(0, &own_addr_type) != 0) {
        Log::error(kTag, "address type inference failed");
        if (self->done_sem_ != nullptr) {
            xSemaphoreGive(self->done_sem_);
        }
        return;
    }

    struct ble_gap_disc_params params = {};
    params.passive = 0;             // active scan: also request scan responses
    params.filter_duplicates = 1;   // controller-side dedup
    params.itvl = 0;                // use stack defaults
    params.window = 0;
    params.limited = 0;

    const int rc = ble_gap_disc(own_addr_type, self->duration_ms_, &params,
                                &BleService::gapEvent, nullptr);
    if (rc != 0) {
        Log::error(kTag, "ble_gap_disc failed: %d", rc);
        if (self->done_sem_ != nullptr) {
            xSemaphoreGive(self->done_sem_);
        }
    }
}

void BleService::hostTask(void* /*param*/) {
    // Runs the NimBLE host until nimble_port_stop() is called, then
    // deletes this task.
    nimble_port_run();
    nimble_port_freertos_deinit();
}

Result<size_t> BleService::scan(BleRecord* out, size_t max,
                                uint32_t duration_ms) {
    if (out == nullptr || max == 0) {
        return Error::InvalidArg;
    }
    if (duration_ms == 0) {
        duration_ms = kDefaultScanMs;
    }
    if (duration_ms > kMaxScanMs) {
        duration_ms = kMaxScanMs;
    }

    bool expected = false;
    if (!scanning_.compare_exchange_strong(expected, true)) {
        return Error::Busy;  // one scan at a time (single shared stack)
    }

    out_ = out;
    max_ = max;
    count_ = 0;
    duration_ms_ = duration_ms;
    done_sem_ = xSemaphoreCreateBinary();
    if (done_sem_ == nullptr) {
        scanning_.store(false, std::memory_order_relaxed);
        return Error::NoMemory;
    }

    // Bring the NimBLE host (and BT controller) up for this scan only.
    const esp_err_t err = nimble_port_init();
    if (err != ESP_OK) {
        vSemaphoreDelete(done_sem_);
        done_sem_ = nullptr;
        scanning_.store(false, std::memory_order_relaxed);
        Log::error(kTag, "nimble_port_init failed: %s", esp_err_to_name(err));
        return fromEspErr(err);
    }

    s_active = this;
    ble_hs_cfg.sync_cb = &BleService::onSync;
    ble_hs_cfg.reset_cb = nullptr;
    nimble_port_freertos_init(&BleService::hostTask);

    // Wait for the discovery to complete (plus margin), then tear down.
    (void)xSemaphoreTake(done_sem_,
                         pdMS_TO_TICKS(duration_ms + 2000));

    const int stop_rc = nimble_port_stop();
    if (stop_rc == 0) {
        nimble_port_deinit();
    } else {
        Log::warn(kTag, "nimble_port_stop returned %d", stop_rc);
    }

    s_active = nullptr;
    vSemaphoreDelete(done_sem_);
    done_sem_ = nullptr;
    out_ = nullptr;
    const size_t found = count_;
    scanning_.store(false, std::memory_order_relaxed);

    Log::info(kTag, "scan complete: %u device(s)",
              static_cast<unsigned>(found));
    return found;
}

}  // namespace gallus::services
