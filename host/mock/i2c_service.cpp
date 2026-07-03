#include "gallus/services/i2c_service.hpp"

namespace gallus::services {

Status I2cService::init(int sda, int scl, uint32_t freq_hz) {
    const Status status = bus_.init(sda, scl, freq_hz);
    if (status.ok()) {
        ready_ = true;
    }
    return status;
}

Result<size_t> I2cService::scan(uint8_t* /*out*/, size_t /*max*/) const {
    if (!ready_) {
        return Error::InvalidState;
    }
    return static_cast<size_t>(0);
}

}  // namespace gallus::services
