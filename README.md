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
- `src/main.cpp`: Wi-Fi, OTA, settings, and hardware startup
- `src/ui.cpp`: Shared LVGL interface
- `src/workflow.cpp`: Confirmed, grouped actions and garage sequencing
- `desktop/`: SDL display/input and simulated Home Assistant backend

## Current UI

The panel uses a monochrome terminal style: JetBrains Mono, black backgrounds,
square white borders, inverted button presses, and explicit state labels. Red is
reserved for a triggered alarm. Individual area controls remain available by
tapping the status rows.

- **Disarm House**: enter a PIN once to disarm Perimeter and Interior; Garage is unchanged.
- **Open / Close Garage**: if necessary, enter a PIN to disarm Garage, wait for its
  confirmed disarmed state, then open the door. An already disarmed garage opens
  directly. When open, the shortcut changes to **Close Garage**, which asks for
  confirmation. The completed-open screen also offers **Close Garage**. Moving or
  unknown door states disable the shortcut.
- **Arm Away**: confirm to arm all three areas in Away mode.
- **Good Night**: confirm to arm all three areas in Night mode.

Commands stay pending until MQTT state updates confirm the result. Grouped
operations show each area's state and a confirmed count. An unconfirmed disarm
never proceeds to open the garage. Door toggles are sent once, and moving or
unknown doors are blocked. Back cancels any remaining steps; commands already
sent may still finish. Home Assistant retains responsibility for automatic garage
rearming. Alarm confirmation times out after 90 seconds; door movement after 20.
These limits should be checked against the actual HA exit delay and door travel time.

## Desktop Simulator

The native SDL simulator compiles the **same UI and workflow C++ code** as the
ESP32. Click the panel with your mouse, or type PIN digits and use Enter,
Backspace, and Escape. The display is shown at 2x size. There is no connection to
Home Assistant, MQTT, or the installed panel: all responses are local simulations,
and no secrets or 1Password access are required.

On macOS, install the build dependencies if needed:

```bash
brew install cmake pkg-config sdl2
```

The build uses the project's existing LVGL 8.4 dependency. On a fresh checkout,
install it first (this downloads libraries without generating runtime secrets):

```bash
pio pkg install -e esp32dev
just desktop
```

Without `just`:

```bash
cmake -S desktop -B .artifacts/desktop -DCMAKE_BUILD_TYPE=Debug
cmake --build .artifacts/desktop --parallel
.artifacts/desktop/alarm-panel
```

If LVGL is elsewhere, configure with `-DLVGL_DIR=/path/to/lvgl-8.4`.

The companion test lab provides:

- Night and Disarmed presets (also reset the door to closed).
- MQTT offline, rejected PIN, 15-second acknowledgement delay, and partial result toggles.
- Triggered alarm, unknown states, display sleep, and a +90-second timeout shortcut.

The fake backend accepts PIN **1234**. Partial results withhold Interior confirmation.
Rejected PINs now emit the same failure event as Alarmo and immediately show
**PIN NOT ACCEPTED** with a **RETRY PIN** button. A retry clears the old PIN and
requests a fresh one; already confirmed areas are skipped. Partial results still
withhold confirmation, so **+90 SEC** exercises a genuinely unresponsive command.

Run the focused workflow checks and real UI interaction smoke test with:

```bash
just desktop-test
```

The smoke test writes `preview-*.bmp` screenshots into `.artifacts/desktop`.
The simulator validates UI and action sequencing; it does not emulate ESP32 timing,
physical touch calibration, Wi-Fi, or OTA. Wokwi remains available for firmware simulation.

The bundled font is licensed under the SIL Open Font License in
`assets/fonts/OFL.txt`. Generated LVGL font sources are committed, so Pillow is
needed only when regenerating fonts:

```bash
python3 scripts/generate_panel_font.py
python3 scripts/generate_panel_font.py --size 40
```

## Alarmo Failure Feedback

The panel subscribes to `alarmo/perimeter/event`, `alarmo/interior/event`, and
`alarmo/garage/event`, following [Alarmo's MQTT event protocol](https://github.com/nielsfaber/alarmo#event-topic).
Alarmo's MQTT **event topic** must be enabled and set to `alarmo/event` for this
default configuration. Custom topic roots or area slugs can be changed in
`src/alarmo_events.cpp`. No Home Assistant automation is required.

- `INVALID_CODE_PROVIDED`: **PIN NOT ACCEPTED**, with fresh PIN retry for disarming.
- `NO_CODE_PROVIDED`: **PIN REQUIRED**, with fresh PIN retry for disarming.
- `COMMAND_NOT_ALLOWED`: **COMMAND NOT ALLOWED**.
- `FAILED_TO_ARM`: **OPEN SENSORS / Unable to arm**.

Failures apply only while waiting on a command sent to that area. Events for
unrelated or already confirmed areas, completed/cancelled operations, unknown
payloads, and mismatched commands are ignored. Group results retain partial
success. A failed garage disarm cancels the door-opening step even if a delayed
state update arrives afterward. Successful actions still require state confirmation.

Alarmo events do not carry a request/client ID, so a simultaneous failure from
another controller targeting the same pending area cannot be distinguished from
this panel's request. Malformed, missing, or oversized event messages fall back
to the existing confirmation timeout. Event JSON is parsed up to 1024 bytes;
the MQTT receive buffer is 1152 bytes. Events should never be published retained.

The Wokwi harness uses simulated PIN `1234` and emits matching per-area failures.
Use `--pin` to change the simulated PIN or `--reject-pin` to reject every disarm.

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

Current MQTT topics include:

- `home/keypad/status`
- `home/keypad/display/set`
- `weather/kbfi/state`
- `garage/door/command`
- `garage/door/state`
- `alarmo/command`
- `alarmo/perimeter/state`
- `alarmo/interior/state`
- `alarmo/garage/state`
- `alarmo/perimeter/event`
- `alarmo/interior/event`
- `alarmo/garage/event`

Current behavior:

- The device connects to WiFi during boot
- The device attempts an MQTT connection after WiFi is up
- The clock uses NTP once WiFi is connected
- The screensaver weather label updates from `weather/kbfi/state`
- The keypad subscribes to per-area Alarmo state/event topics and `garage/door/state`
- The main screen shows shortcuts and Perimeter, Interior, Garage, and Door status rows
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
- `alarmo/perimeter/state`
- `alarmo/interior/state`
- `alarmo/garage/state`
- `alarmo/perimeter/event`
- `alarmo/interior/event`
- `alarmo/garage/event`
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
- LVGL renders the shared monochrome control screen
- Actions publish MQTT commands and wait for state confirmation

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

## Wokwi Serial Fallback

The existing Wokwi diagram still uses serial controls rather than clickable touch.
Use the native desktop simulator for normal UI development. Wokwi shortcuts:

- `u`: Disarm House; `o`: Open Garage; `w`: Arm Away; `n`: Good Night
- `p`, `i`, `g`, `d`: individual Perimeter, Interior, Garage, Door controls
- `0`–`9`: PIN; `s` or Enter: submit/confirm; `x`: delete; `c`: back/cancel
- `h` or `v`: Home or Vacation when an individual area's arm-mode chooser is open
