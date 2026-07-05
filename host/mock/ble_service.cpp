#include "gallus/services/ble_service.hpp"

// Host stub: no BLE radio off-target. A scan finds nothing.

namespace gallus::services {

Result<size_t> BleService::scan(BleRecord* out, size_t max,
                                uint32_t /*duration_ms*/) {
    if (out == nullptr || max == 0) {
        return Error::InvalidArg;
    }
    return static_cast<size_t>(0);
}

}  // namespace gallus::services
