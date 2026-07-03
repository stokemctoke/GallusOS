#include "gallus/drivers/i2c_bus.hpp"

#include "gallus/log.hpp"

namespace gallus::drivers {

namespace {
constexpr const char* kTag = "I2C";
constexpr int kProbeTimeoutMs = 50;
constexpr int kWriteTimeoutMs = 1000;
}  // namespace

Status I2cBus::init(int sda, int scl, uint32_t freq_hz) {
    if (bus_ != nullptr) {
        return Error::InvalidState;
    }
    freq_hz_ = freq_hz;

    i2c_master_bus_config_t cfg = {};
    cfg.i2c_port = I2C_NUM_0;
    cfg.sda_io_num = static_cast<gpio_num_t>(sda);
    cfg.scl_io_num = static_cast<gpio_num_t>(scl);
    cfg.clk_source = I2C_CLK_SRC_DEFAULT;
    cfg.glitch_ignore_cnt = 7;
    cfg.flags.enable_internal_pullup = true;

    const esp_err_t err = i2c_new_master_bus(&cfg, &bus_);
    if (err != ESP_OK) {
        bus_ = nullptr;
        Log::error(kTag, "bus init failed: %s", esp_err_to_name(err));
        return fromEspErr(err);
    }
    Log::info(kTag, "bus up on SDA=%d SCL=%d @ %u Hz", sda, scl,
              static_cast<unsigned>(freq_hz));
    return Status::success();
}

Status I2cBus::addDevice(uint8_t address, i2c_master_dev_handle_t* out) {
    if (bus_ == nullptr || out == nullptr) {
        return Error::InvalidState;
    }

    i2c_device_config_t dev_cfg = {};
    dev_cfg.dev_addr_length = I2C_ADDR_BIT_LEN_7;
    dev_cfg.device_address = address;
    dev_cfg.scl_speed_hz = freq_hz_;

    return fromEspErr(i2c_master_bus_add_device(bus_, &dev_cfg, out));
}

Status I2cBus::write(i2c_master_dev_handle_t dev, const uint8_t* data,
                     size_t len) {
    if (dev == nullptr || data == nullptr) {
        return Error::InvalidArg;
    }
    return fromEspErr(i2c_master_transmit(dev, data, len, kWriteTimeoutMs));
}

bool I2cBus::probe(uint8_t address) const {
    if (bus_ == nullptr) {
        return false;
    }
    return i2c_master_probe(bus_, address, kProbeTimeoutMs) == ESP_OK;
}

}  // namespace gallus::drivers
