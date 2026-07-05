#include "gallus/services/ieee802154_service.hpp"

// Host stub: no 802.15.4 radio off-target. A survey finds nothing.

namespace gallus::services {

Result<size_t> Ieee802154Service::scan(ChannelRecord* out, size_t max,
                                       uint32_t /*dwell_ms*/) {
    if (out == nullptr || max == 0) {
        return Error::InvalidArg;
    }
    return static_cast<size_t>(0);
}

}  // namespace gallus::services
