#include "gallus/services/i2c_service.hpp"

namespace gallus::services {

Status I2cService::init(int sda, int scl, uint32_t freq_hz) {
    const Status status = bus_.init(sda, scl, freq_hz);
    if (status.ok()) {
        ready_ = true;
    }
    return status;
}

Result<size_t> I2cService::scan(uint8_t* out, size_t max) const {
    if (!ready_) {
        return Error::InvalidState;
    }
    static constexpr uint8_t kMockDevices[] = {0x3C, 0x48};
    const size_t count =
        sizeof(kMockDevices) / sizeof(kMockDevices[0]) < max
            ? sizeof(kMockDevices) / sizeof(kMockDevices[0])
            : max;
    for (size_t i = 0; i < count; ++i) {
        out[i] = kMockDevices[i];
    }
    return count;
}

}  // namespace gallus::services
