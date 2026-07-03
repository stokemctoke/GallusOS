#include "gallus/drivers/gpio.hpp"

namespace gallus::drivers {

Status GpioDriver::configure(int /*pin*/, PinMode /*mode*/) {
    return Status::success();
}

Status GpioDriver::write(int /*pin*/, bool /*level*/) {
    return Status::success();
}

Result<bool> GpioDriver::read(int /*pin*/) {
    return false;
}

Status GpioDriver::reset(int /*pin*/) {
    return Status::success();
}

}  // namespace gallus::drivers
