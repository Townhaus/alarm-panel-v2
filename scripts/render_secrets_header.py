#!/usr/bin/env python3
"""
Generate include/secrets.h from 1Password item fields.
"""

from __future__ import annotations

import subprocess
import sys
import os
import argparse
import tempfile
from pathlib import Path


OUTPUT_PATH = Path(__file__).resolve().parents[1] / "include" / "secrets.h"
DEFAULT_VAULT = "Homelab"
DEFAULT_ITEM = "Alarm Panel"

SECRET_FIELDS = [
    ("WIFI_SSID", "wifi_ssid"),
    ("WIFI_PASSWORD", "wifi_password"),
    ("MQTT_HOST", "mqtt_host"),
    ("MQTT_PORT", "mqtt_port"),
    ("MQTT_CLIENT_ID", "mqtt_client_id"),
    ("MQTT_USERNAME", "mqtt_username"),
    ("MQTT_PASSWORD", "mqtt_password"),
    ("OTA_HOSTNAME", "ota_hostname"),
    ("OTA_PASSWORD", "ota_password"),
]

TOPIC_CONSTANTS = [
    ("MQTT_TOPIC_GARAGE", "home/keypad/garage/toggle"),
    ("MQTT_TOPIC_STATUS", "home/keypad/status"),
    ("MQTT_TOPIC_DISPLAY_SET", "home/keypad/display/set"),
    ("MQTT_TOPIC_GARAGE_DOOR_COMMAND", "garage/door/command"),
    ("MQTT_TOPIC_GARAGE_DOOR_STATE", "garage/door/state"),
    ("MQTT_TOPIC_WEATHER_STATE", "weather/kbfi/state"),
    ("ALARMO_COMMAND_TOPIC", "alarmo/command"),
    ("ALARMO_PERIMETER_STATE_TOPIC", "alarmo/perimeter/state"),
    ("ALARMO_INTERIOR_STATE_TOPIC", "alarmo/interior/state"),
    ("ALARMO_GARAGE_STATE_TOPIC", "alarmo/garage/state"),
    ("MQTT_TOPIC_DISPLAY_TIMEOUT", "home/keypad/display/timeout"),
]


def op_read(reference: str) -> str:
    try:
        result = subprocess.run(
            ["op", "read", "--no-newline", reference],
            check=True,
            capture_output=True,
            text=True,
        )
    except FileNotFoundError:
        raise SystemExit("1Password CLI `op` is not installed or not on PATH.") from None
    except subprocess.CalledProcessError:
        raise SystemExit(f"Failed to read 1Password reference {reference!r}; check access to the item.") from None
    return result.stdout


def c_string(value: str) -> str:
    escaped = "".join(
        "\\\\" if byte == 92 else '\\"' if byte == 34 else
        chr(byte) if 32 <= byte < 127 else f"\\{byte:03o}"
        for byte in value.encode("utf-8")
    )
    return f'"{escaped}"'


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, default=OUTPUT_PATH)
    args = parser.parse_args()
    vault = os.getenv("OP_VAULT", DEFAULT_VAULT)
    item = os.getenv("OP_ITEM", DEFAULT_ITEM)

    secrets: dict[str, str] = {}
    for constant_name, field_name in SECRET_FIELDS:
        reference = f"op://{vault}/{item}/{field_name}"
        secrets[constant_name] = os.environ.get("ALARM_PANEL_" + constant_name)
        if secrets[constant_name] is None:
            secrets[constant_name] = op_read(reference)

    try:
        mqtt_port = int(secrets["MQTT_PORT"])
        if not 1 <= mqtt_port <= 65535:
            raise ValueError
    except ValueError:
        raise SystemExit("MQTT_PORT must be an integer between 1 and 65535.") from None

    lines = [
        "#pragma once",
        "",
        "#include <Arduino.h>",
        "",
        "namespace Secrets {",
        f"constexpr char WIFI_SSID[] = {c_string(secrets['WIFI_SSID'])};",
        f"constexpr char WIFI_PASSWORD[] = {c_string(secrets['WIFI_PASSWORD'])};",
        f"constexpr char MQTT_HOST[] = {c_string(secrets['MQTT_HOST'])};",
        f"constexpr uint16_t MQTT_PORT = {mqtt_port};",
        f"constexpr char MQTT_CLIENT_ID[] = {c_string(secrets['MQTT_CLIENT_ID'])};",
        f"constexpr char MQTT_USERNAME[] = {c_string(secrets['MQTT_USERNAME'])};",
        f"constexpr char MQTT_PASSWORD[] = {c_string(secrets['MQTT_PASSWORD'])};",
        f"constexpr char OTA_HOSTNAME[] = {c_string(secrets['OTA_HOSTNAME'])};",
        f"constexpr char OTA_PASSWORD[] = {c_string(secrets['OTA_PASSWORD'])};",
        "",
    ]

    for constant_name, value in TOPIC_CONSTANTS:
        lines.append(f"constexpr char {constant_name}[] = {c_string(value)};")

    lines.append("}  // namespace Secrets")
    lines.append("")

    args.output.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.NamedTemporaryFile(mode="w", encoding="utf-8", dir=args.output.parent, delete=False) as temporary:
        temporary.write("\n".join(lines))
        temporary_path = Path(temporary.name)
    try:
        temporary_path.chmod(0o600)
        temporary_path.replace(args.output)
    finally:
        temporary_path.unlink(missing_ok=True)
    print(f"Wrote {args.output}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
