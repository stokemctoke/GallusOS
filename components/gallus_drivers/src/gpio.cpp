#include "gallus/drivers/gpio.hpp"

#include "driver/gpio.h"

namespace gallus::drivers {

Status GpioDriver::configure(int pin, PinMode mode) {
    gpio_config_t cfg = {};
    cfg.pin_bit_mask = 1ULL << pin;
    cfg.intr_type = GPIO_INTR_DISABLE;
    cfg.pull_up_en = GPIO_PULLUP_DISABLE;
    cfg.pull_down_en = GPIO_PULLDOWN_DISABLE;

    switch (mode) {
        case PinMode::Disabled:
            cfg.mode = GPIO_MODE_DISABLE;
            break;
        case PinMode::Input:
            cfg.mode = GPIO_MODE_INPUT;
            break;
        case PinMode::InputPullUp:
            cfg.mode = GPIO_MODE_INPUT;
            cfg.pull_up_en = GPIO_PULLUP_ENABLE;
            break;
        case PinMode::InputPullDown:
            cfg.mode = GPIO_MODE_INPUT;
            cfg.pull_down_en = GPIO_PULLDOWN_ENABLE;
            break;
        case PinMode::Output:
            cfg.mode = GPIO_MODE_OUTPUT;
            break;
        case PinMode::OutputOpenDrain:
            cfg.mode = GPIO_MODE_OUTPUT_OD;
            break;
    }

    return fromEspErr(gpio_config(&cfg));
}

Status GpioDriver::write(int pin, bool level) {
    return fromEspErr(
        gpio_set_level(static_cast<gpio_num_t>(pin), level ? 1 : 0));
}

Result<bool> GpioDriver::read(int pin) {
    return gpio_get_level(static_cast<gpio_num_t>(pin)) != 0;
}

Status GpioDriver::reset(int pin) {
    return fromEspErr(gpio_reset_pin(static_cast<gpio_num_t>(pin)));
}

}  // namespace gallus::drivers
