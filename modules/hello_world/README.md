# hello_world

The smallest possible GallusOS module — a template for writing your own.

Logs a greeting at start and a heartbeat line periodically.

## Configuration (`/fs/config/hello_world.json`)

| Key | Type | Default | Meaning |
|---|---|---|---|
| `message` | string | `Hello from GallusOS!` | Greeting logged at start |
| `period_s` | int | `60` | Seconds between "still here" log lines |

Disable the module by setting `hello_world: false` in the `modules`
config namespace (`/fs/config/modules.json`).
