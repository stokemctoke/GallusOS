#include "gallus/drivers/i2c_bus.hpp"

namespace gallus::drivers {

Status I2cBus::init(int /*sda*/, int /*scl*/, uint32_t /*freq_hz*/) {
    bus_ = reinterpret_cast<i2c_master_bus_handle_t>(1);
    return Status::success();
}

Status I2cBus::addDevice(uint8_t /*address*/,
                         i2c_master_dev_handle_t* out_handle) {
    if (out_handle == nullptr) {
        return Error::InvalidArg;
    }
    *out_handle = reinterpret_cast<i2c_master_dev_handle_t>(1);
    return Status::success();
}

Status I2cBus::write(i2c_master_dev_handle_t /*dev*/, const uint8_t* /*data*/,
                     size_t /*len*/) {
    return Status::success();
}

bool I2cBus::probe(uint8_t /*address*/) const {
    return false;
}

}  // namespace gallus::drivers
