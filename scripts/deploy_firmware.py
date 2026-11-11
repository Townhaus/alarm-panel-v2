#!/usr/bin/env python3
"""Build in a private staging directory and optionally perform authenticated OTA."""

import argparse
from contextlib import redirect_stderr, redirect_stdout
from datetime import datetime, timezone
import hashlib
import importlib.util
import io
import ipaddress
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def stage_sources(root, workspace):
    for directory in ("src", "include"):
        shutil.copytree(root / directory, workspace / directory,
                        ignore=shutil.ignore_patterns("secrets.h", "__pycache__"))
    for filename in ("platformio.ini", "partitions_ota_no_fs.csv"):
        shutil.copy2(root / filename, workspace / filename)


def upload(firmware, target, host_ip, host_port):
    core = Path(os.environ.get("PLATFORMIO_CORE_DIR", Path.home() / ".platformio"))
    uploader = core / "packages/framework-arduinoespressif32/tools/espota.py"
    if not uploader.is_file():
        raise RuntimeError("The PlatformIO Arduino ESP32 OTA tool is unavailable; build first.")
    spec = importlib.util.spec_from_file_location("alarm_panel_espota", uploader)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    # Password exists only in memory/environment, not the OS process arguments.
    arguments = ["-i", target, "-I", host_ip, "-p", "3232", "-P", str(host_port),
                 "-a", os.environ["ALARM_PANEL_OTA_PASSWORD"], "-f", str(firmware)]
    with redirect_stdout(io.StringIO()), redirect_stderr(io.StringIO()):
        try:
            result = module.main(arguments)
        except SystemExit as exc:
            result = exc.code
    if result:
        raise RuntimeError("OTA failed. Check the target, current OTA password, and TCP callback firewall rule.")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--mode", choices=("build", "deploy"), default="build")
    parser.add_argument("--target", default="")
    parser.add_argument("--expected-ip", default="")
    parser.add_argument("--host-ip", default="")
    parser.add_argument("--host-port", type=int, default=3232)
    args = parser.parse_args()
    if args.mode == "deploy":
        for address in (args.target, args.expected_ip, args.host_ip):
            try:
                ipaddress.IPv4Address(address)
            except ipaddress.AddressValueError:
                parser.error("OTA deployment requires explicit current, expected and uploader IPv4 addresses")
    if not 1024 <= args.host_port <= 65535:
        parser.error("Callback port must be between 1024 and 65535")
    if os.environ.get("ALARM_PANEL_WIFI_SSID") != "GREATWHITE-IoT":
        parser.error("This deployment workflow targets GREATWHITE-IoT")
    for name in ("WIFI_PASSWORD", "MQTT_HOST", "MQTT_PORT", "MQTT_CLIENT_ID",
                 "MQTT_USERNAME", "MQTT_PASSWORD", "OTA_HOSTNAME", "OTA_PASSWORD"):
        if not os.environ.get("ALARM_PANEL_" + name):
            parser.error(f"Missing ALARM_PANEL_{name}; run scripts/deploy.sh through 1Password")
    if not shutil.which("pio"):
        parser.error("PlatformIO CLI pio is required on the deployment machine")

    os.umask(0o077)
    artifact_root = ROOT / ".artifacts"
    artifact_root.mkdir(mode=0o700, exist_ok=True)
    artifact = Path(tempfile.mkdtemp(prefix="firmware-", dir=artifact_root))
    revision = subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=ROOT, text=True).strip()
    summary = {
        "artifact_directory": str(artifact), "source_revision": revision,
        "source_dirty": bool(subprocess.check_output(["git", "status", "--porcelain"], cwd=ROOT)),
        "ssid": "GREATWHITE-IoT", "mqtt_host": os.environ["ALARM_PANEL_MQTT_HOST"],
        "mqtt_port": int(os.environ["ALARM_PANEL_MQTT_PORT"]),
        "built_utc": datetime.now(timezone.utc).isoformat(), "uploaded": False,
    }
    with tempfile.TemporaryDirectory(prefix="alarm-panel-build-") as temporary:
        workspace = Path(temporary)
        stage_sources(ROOT, workspace)
        steps = [
            [sys.executable, str(ROOT / "scripts/render_secrets_header.py"),
             "--output", str(workspace / "include/secrets.h")],
            ["pio", "run", "--project-dir", str(workspace), "--environment", "esp32dev"],
        ]
        for step, command in zip(("Secret header rendering", "Firmware build"), steps):
            result = subprocess.run(command, cwd=workspace, capture_output=True, text=True)
            if result.returncode:
                # Compiler/CLI output can contain secrets embedded in source.
                raise RuntimeError(f"{step} failed. No compiler output or credential values were printed.")
        firmware = artifact / "firmware.bin"
        shutil.copyfile(workspace / ".pio/build/esp32dev/firmware.bin", firmware)
        firmware.chmod(0o600)
        summary["firmware_sha256"] = hashlib.sha256(firmware.read_bytes()).hexdigest()
        summary["firmware_bytes"] = firmware.stat().st_size
    (artifact / "manifest.json").write_text(json.dumps(summary, indent=2) + "\n")
    if args.mode == "deploy":
        upload(firmware, args.target, args.host_ip, args.host_port)
        summary.update(uploaded=True, upload_target=args.target, expected_ip=args.expected_ip)
        (artifact / "manifest.json").write_text(json.dumps(summary, indent=2) + "\n")
    # This confirms the uploader acknowledged the transfer, not MQTT/alarm
    # functionality after reboot. Verify the panel's status before relying on it.
    print(json.dumps(summary))
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (OSError, ValueError, KeyError, RuntimeError):
        print("Alarm panel deployment failed; no credentials were printed. Check tool availability, build compatibility and network/OTA settings.", file=sys.stderr)
        sys.exit(1)
