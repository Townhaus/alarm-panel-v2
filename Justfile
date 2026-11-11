# Wokwi simulation workflow
# Requires: pio, uv, wokwi-cli, op (1Password CLI)

# Build using the IoT Wi-Fi credentials; upload only with explicit Ansible vars.
deploy *args:
    bash scripts/deploy.sh {{args}}

# Build firmware and launch sim + MQTT harness in parallel
sim: build
    #!/usr/bin/env bash
    set -euo pipefail
    uv run --with paho-mqtt python3 scripts/wokwi_mqtt_harness.py &
    harness_pid=$!
    trap 'kill "$harness_pid" 2>/dev/null || true' EXIT INT TERM
    WOKWI_CLI_TOKEN="$(op read "${OP_WOKWI_TOKEN_REF:-op://Homelab/Wokwi CLI/credential}")" \
      wokwi-cli \
      --interactive \
      --timeout "${WOKWI_TIMEOUT_MS:-300000}" \
      --timeout-exit-code 0 \
      .

# Build the Wokwi firmware only
build:
    python3 scripts/render_secrets_header.py
    pio run -e esp32dev-wokwi

# Build and flash to device over OTA
flash target='':
    #!/usr/bin/env bash
    set -euo pipefail
    python3 scripts/render_secrets_header.py
    export OTA_PASSWORD="$(op read "${OP_OTA_PASSWORD_REF:-op://Homelab/Alarm Panel/ota_password}")"
    ota_hostname="$(op read "${OP_OTA_HOSTNAME_REF:-op://Homelab/Alarm Panel/ota_hostname}")"
    upload_target="${target:-${OTA_UPLOAD_PORT:-${ota_hostname}.local}}"
    pio run -e esp32dev-ota --target upload --upload-port "$upload_target"

# Run only the MQTT harness (useful for debugging against a real device)
harness:
    uv run --with paho-mqtt python3 scripts/wokwi_mqtt_harness.py

# Install Python deps for the harness
setup:
    uv tool install platformio
