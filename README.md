# ESP32 Home Keypad Proof of Concept

PlatformIO project for an `ESP32-WROOM-32` driving an `ILI9341` 240x320 SPI TFT with an `XPT2046` resistive touch controller. The UI is built with `LVGL`, display rendering uses `TFT_eSPI`, and touch input is handled on a separate SPI bus.

## Hardware

### Target MCU

- ESP32-WROOM-32
- PlatformIO board: `esp32dev`
- Framework: `Arduino`

### Wiring

| Signal | ESP32 GPIO | Notes |
| --- | --- | --- |
| TFT_MOSI / SDI | GPIO13 | Display SPI MOSI |
| TFT_MISO / SDO | GPIO12 | Display SPI MISO, boot strapping pin |
| TFT_SCLK / SCK | GPIO14 | Display SPI clock |
| TFT_CS | GPIO15 | Display chip select |
| TFT_DC | GPIO2 | Display data/command |
| TFT_RST | GPIO4 | Display reset |
| TFT_LED | GPIO21 | Backlight enable |
| TOUCH_MOSI / T_DIN | GPIO32 | Touch SPI MOSI |
| TOUCH_MISO / T_OUT | GPIO39 | Touch SPI MISO, input-only is expected |
| TOUCH_SCLK / T_CLK | GPIO25 | Touch SPI clock |
| TOUCH_CS | GPIO33 | Touch chip select |
| TOUCH_IRQ / T_IRQ | GPIO36 | Touch interrupt, input-only is expected |
| VCC | 3.3V or 5V | Module-dependent |
| GND | GND | Common ground |

## Notes

- The display is configured for portrait mode `240x320`.
- `GPIO36` and `GPIO39` are input-only, which is appropriate for `T_IRQ` and `T_OUT`.
- `GPIO12` is a boot strapping pin on ESP32. If boot issues occur, move display `MISO` off `GPIO12` and update `include/pin_config.h` plus `platformio.ini` build flags.
- `TFT_eSPI` is configured locally through `platformio.ini` build flags. No global library files need to be edited.
- Touch calibration values in `include/pin_config.h` are safe starter values and will likely need tuning on real hardware.
- Runtime secrets are sourced from 1Password and rendered into `include/secrets.h`. That generated file is gitignored and should be treated as a build artifact, not the source of truth.
- The project uses a custom OTA partition table with no SPIFFS/LittleFS partition so both OTA app slots are larger.

## Project Layout

- `platformio.ini`: PlatformIO environment and TFT_eSPI build-time configuration
- `include/pin_config.h`: Pin definitions and touch calibration bounds
- `include/lv_conf.h`: Local LVGL configuration
- `include/secrets.example.h`: Template showing the generated header shape
- `include/secrets.h`: Generated local secrets header from 1Password
- `scripts/render_secrets_header.py`: Generates `include/secrets.h` from 1Password
- `src/main.cpp`: Hardware setup, LVGL integration, UI, and placeholder actions

## Current UI

- Top row with local time and weather summary
- `Master` alarm status card
- `Garage` alarm status card
- `Garage Door` status card
- Tap a card to open an action modal
- Alarm arming uses direct mode buttons
- Alarm disarm requires PIN entry
- Garage door modal shows `Open` or `Close` based on current door state

## MQTT Setup

The project now includes a first-pass WiFi and MQTT client using `PubSubClient`.

## 1Password Schema

Create a 1Password item named `Alarm Panel` in vault `Homelab` with these fields:

- `wifi_ssid`
- `wifi_password`
- `mqtt_host`
- `mqtt_port`
- `mqtt_client_id`
- `mqtt_username`
- `mqtt_password`
- `ota_hostname`
- `ota_password`

Create or keep a separate `Wokwi CLI` item in the same vault with:

- `credential`

You can override those default references at runtime with:

- `OP_VAULT`
- `OP_ITEM`
- `OP_OTA_HOSTNAME_REF`
- `OP_OTA_PASSWORD_REF`
- `OP_WOKWI_TOKEN_REF`

Generate the local header with:

```bash
python3 scripts/render_secrets_header.py
```

Current publish topics are:

- `home/keypad/status`
- `home/keypad/display/set`
- `weather/kbfi/state`
- `garage/door/command`
- `garage/door/state`
- `alarmo/command`
- `alarmo/state`
- `alarmo/garage/state`

Current behavior:

- The device connects to WiFi during boot
- The device attempts an MQTT connection after WiFi is up
- The top-left clock uses NTP once WiFi is connected
- The top-right weather label updates from `weather/kbfi/state`
- The keypad subscribes to `alarmo/state`, `alarmo/garage/state`, and `garage/door/state`
- The main screen shows master alarm, garage alarm, and garage door status with color dots
- Alarm arming does not require a PIN
- Alarm disarm requires a PIN
- Garage door actions publish `TOGGLE` to `garage/door/command`
- The display backlight can be controlled over MQTT and wakes on touch
- The status topic publishes `online` as a retained message on connect

## Display Sleep

The display backlight can be controlled over MQTT using:

- Topic: `home/keypad/display/set`
- Payloads: `ON`, `OFF`, `WAKE`, `SLEEP`, `TOGGLE`

Behavior:

- `OFF` or `SLEEP` turns the backlight off
- `ON` or `WAKE` turns the backlight on
- `TOGGLE` flips the current backlight state
- A touch while sleeping wakes the display and does not trigger a UI action

## Build and Upload

For the IoT Wi-Fi migration and repeatable deployment with Ansible, see
[the Ansible deployment guide](ansible/README.md). `bash scripts/deploy.sh`
builds real hardware firmware using the shared IoT password from 1Password;
uploading requires an explicit option and target addresses. The legacy commands
below remain available for simulation and the original 1Password item schema.

Run these commands from the project root:

```bash
just build
just flash
pio device monitor
```

## OTA Updates

The project includes `ArduinoOTA` so you can update over Wi-Fi after the first USB flash.

Configure these in the 1Password `Alarm Panel` item:

- `ota_hostname`
- `ota_password`

After the device joins Wi-Fi, it advertises itself as:

- `OTA_HOSTNAME.local`

First flash over USB:

```bash
pio run -e esp32dev -t upload
```

Wireless OTA upload:

```bash
just flash
```

Override the OTA upload target explicitly when needed:

```bash
just flash 192.168.1.123
just flash alarm-panel.local
```

Notes:

- The first flash still needs USB.
- The device must be on Wi-Fi and powered on.
- `platformio.ini` uses `OTA_PASSWORD` from the environment at upload time.
- `just flash` reads `ota_password` and `ota_hostname` from 1Password at upload time.
- If no explicit target is given, `just flash` uploads to `ota_hostname.local`.
- OTA progress is logged to Serial if a monitor is attached.
- If mDNS is unreliable on your network, override the upload host:

```bash
just flash 192.168.1.123
```

## Wokwi Simulation

The project includes Wokwi files:

- `diagram.json`
- `wokwi.toml`
- `env:esp32dev-wokwi` in `platformio.ini`

Build for Wokwi:

```bash
just build
```

When built with `env:esp32dev-wokwi`, firmware automatically uses:

- Wi-Fi SSID: `Wokwi-GUEST`
- Wi-Fi password: empty
- MQTT broker: `broker.hivemq.com:1883`

Your normal hardware environments (`esp32dev`, `esp32dev-ota`) continue to use values from `include/secrets.h`.

### MQTT Harness For Wokwi

Use the included helper to mimic Home Assistant-like topic updates and log keypad commands:

```bash
just harness
```

`just sim` reads the Wokwi token from `op://Homelab/Wokwi CLI/credential`. Override the reference with `OP_WOKWI_TOKEN_REF` if your item lives elsewhere.
`just sim` runs the headless Wokwi CLI. It does not open the browser-based visual simulator; it streams serial logs in the terminal and exits after `WOKWI_TIMEOUT_MS` milliseconds. The default is `300000` (5 minutes).

The harness publishes:

- `weather/kbfi/state` JSON payloads
- `alarmo/state`
- `alarmo/garage/state`
- `garage/door/state`

And listens for:

- `alarmo/command`
- `garage/door/command`
- `home/keypad/display/set`
- `home/keypad/status`

## What Happens Today

- The backlight is enabled on `GPIO21`
- The display is initialized with reset on `GPIO4`
- Touch is initialized on its own SPI bus
- LVGL renders a simple proof-of-concept keypad screen
- Button presses log to Serial

## Touch Calibration

If touches land in the wrong place, adjust the touch settings in `include/pin_config.h`.

Orientation flags:

- `TOUCH_SWAP_XY`
- `TOUCH_INVERT_X`
- `TOUCH_INVERT_Y`

Edge alignment values:

- `TOUCH_RAW_MIN_X`
- `TOUCH_RAW_MAX_X`
- `TOUCH_RAW_MIN_Y`
- `TOUCH_RAW_MAX_Y`

Recommended process:

1. Upload the firmware and open `pio device monitor`
2. Touch near the top-left, top-right, bottom-left, and bottom-right corners
3. Watch the `[TOUCH] raw=(...) mapped=(...)` logs
4. If the touch moves along the wrong axis, change `TOUCH_SWAP_XY`
5. If the touch is mirrored left/right or up/down, change the corresponding invert flag
6. If the touch reaches the right area but is offset near the edges, tighten the raw min/max values

For example:

- If pressing higher on the screen makes `mapped.x` change instead of `mapped.y`, flip `TOUCH_SWAP_XY`
- If pressing the left side triggers the right side, toggle `TOUCH_INVERT_X`
- If pressing the top triggers the bottom, toggle `TOUCH_INVERT_Y`

## Next Steps

- Add WiFi setup in `setup()` before any MQTT connection logic
- Add MQTT client initialization after WiFi is connected
- Replace placeholder publish functions with real MQTT publishes
- Update `MQTT: disconnected` from actual connection state
- Add a PIN pad modal or separate screen for arm/disarm workflows
- Calibrate touch and refine coordinate mapping on the physical device
