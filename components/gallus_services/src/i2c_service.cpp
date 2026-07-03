#include "gallus/services/i2c_service.hpp"

namespace gallus::services {

Status I2cService::init(int sda, int scl, uint32_t freq_hz) {
    if (ready_) {
        return Error::InvalidState;
    }
    GALLUS_RETURN_IF_ERROR(bus_.init(sda, scl, freq_hz));
    ready_ = true;
    return Status::success();
}

Result<size_t> I2cService::scan(uint8_t* out, size_t max) const {
    if (!ready_) {
        return Error::InvalidState;
    }
    if (out == nullptr || max == 0) {
        return Error::InvalidArg;
    }

    size_t found = 0;
    for (uint8_t addr = 0x03; addr <= 0x77 && found < max; ++addr) {
        if (bus_.probe(addr)) {
            out[found++] = addr;
        }
    }
    return found;
}

}  // namespace gallus::services
