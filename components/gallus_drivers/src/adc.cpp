#include "gallus/drivers/adc.hpp"

#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_adc/adc_oneshot.h"

#include "gallus/log.hpp"

namespace gallus::drivers {

namespace {
constexpr const char* kTag = "ADC";
}

Adc::~Adc() {
    if (cali_ != nullptr) {
#if ADC_CALI_SCHEME_CURVE_FITTING_SUPPORTED
        adc_cali_delete_scheme_curve_fitting(
            static_cast<adc_cali_handle_t>(cali_));
#endif
    }
    if (unit_ != nullptr) {
        adc_oneshot_del_unit(static_cast<adc_oneshot_unit_handle_t>(unit_));
    }
}

Status Adc::init(int gpio) {
    if (unit_ != nullptr) {
        return Error::InvalidState;
    }

    adc_unit_t unit = ADC_UNIT_1;
    adc_channel_t channel = ADC_CHANNEL_0;
    esp_err_t err = adc_oneshot_io_to_channel(gpio, &unit, &channel);
    if (err != ESP_OK) {
        Log::error(kTag, "GPIO %d is not an ADC pin", gpio);
        return fromEspErr(err);
    }
    channel_ = channel;

    adc_oneshot_unit_handle_t unit_handle = nullptr;
    adc_oneshot_unit_init_cfg_t unit_cfg = {};
    unit_cfg.unit_id = unit;
    err = adc_oneshot_new_unit(&unit_cfg, &unit_handle);
    if (err != ESP_OK) {
        return fromEspErr(err);
    }
    unit_ = unit_handle;

    adc_oneshot_chan_cfg_t chan_cfg = {};
    chan_cfg.atten = ADC_ATTEN_DB_12;
    chan_cfg.bitwidth = ADC_BITWIDTH_DEFAULT;
    err = adc_oneshot_config_channel(unit_handle, channel, &chan_cfg);
    if (err != ESP_OK) {
        return fromEspErr(err);
    }

#if ADC_CALI_SCHEME_CURVE_FITTING_SUPPORTED
    adc_cali_handle_t cali_handle = nullptr;
    adc_cali_curve_fitting_config_t cali_cfg = {};
    cali_cfg.unit_id = unit;
    cali_cfg.chan = channel;
    cali_cfg.atten = ADC_ATTEN_DB_12;
    cali_cfg.bitwidth = ADC_BITWIDTH_DEFAULT;
    if (adc_cali_create_scheme_curve_fitting(&cali_cfg, &cali_handle) ==
        ESP_OK) {
        cali_ = cali_handle;
        calibrated_ = true;
    } else {
        Log::warn(kTag, "calibration unavailable, using raw scaling");
    }
#endif

    return Status::success();
}

Result<int> Adc::readMillivolts(int samples) {
    if (unit_ == nullptr) {
        return Error::InvalidState;
    }
    if (samples < 1) {
        samples = 1;
    }

    auto unit = static_cast<adc_oneshot_unit_handle_t>(unit_);
    int64_t accum = 0;
    for (int i = 0; i < samples; ++i) {
        int raw = 0;
        const esp_err_t err = adc_oneshot_read(
            unit, static_cast<adc_channel_t>(channel_), &raw);
        if (err != ESP_OK) {
            return fromEspErr(err);
        }

        int mv = raw;
        if (calibrated_) {
            if (adc_cali_raw_to_voltage(static_cast<adc_cali_handle_t>(cali_),
                                        raw, &mv) != ESP_OK) {
                mv = raw;
            }
        }
        accum += mv;
    }
    return static_cast<int>(accum / samples);
}

}  // namespace gallus::drivers
