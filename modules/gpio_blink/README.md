# gpio_blink

Blinks a GPIO through the reservation manager — the reference example
for safe pin access from a module.

## Configuration (`/fs/config/gpio_blink.json`)

| Key | Type | Default | Meaning |
|---|---|---|---|
| `pin` | int | `27` (onboard user LED) | GPIO to blink |
| `period_ms` | int | `1000` | Toggle interval in milliseconds |

Publishes a `GPIOChanged` event on every toggle.

Disable the module by setting `gpio_blink: false` in the `modules`
config namespace (`/fs/config/modules.json`).
