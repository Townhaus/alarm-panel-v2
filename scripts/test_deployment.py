#!/usr/bin/env python3
"""Offline tests for secret handling and firmware staging; never upload hardware."""

import importlib.util
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch


def load(name, filename):
    spec = importlib.util.spec_from_file_location(name, Path(__file__).with_name(filename))
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


renderer = load("renderer", "render_secrets_header.py")
deployment = load("deployment", "deploy_firmware.py")


class DeploymentTests(unittest.TestCase):
    def test_c_literals_escape_controls_quotes_and_unicode(self):
        result = renderer.c_string(' a"\\\n\x00é')
        self.assertEqual(result, '" a\\"\\\\\\012\\000\\303\\251"')
        self.assertNotIn("\n", result)

    def test_render_uses_environment_without_reading_op_and_writes_private_file(self):
        values = {"ALARM_PANEL_" + key: "test-value" for key, _ in renderer.SECRET_FIELDS}
        values["ALARM_PANEL_MQTT_PORT"] = "1883"
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory) / "secrets.h"
            with patch.dict(os.environ, values), patch.object(sys, "argv", ["render", "--output", str(output)]), patch.object(renderer, "op_read", side_effect=AssertionError("unexpected op read")):
                self.assertEqual(renderer.main(), 0)
            self.assertEqual(output.stat().st_mode & 0o777, 0o600)
            self.assertIn("constexpr uint16_t MQTT_PORT = 1883;", output.read_text())

    def test_op_read_failure_does_not_print_subprocess_output(self):
        error = subprocess.CalledProcessError(1, ["op"], output="sensitive-stdout", stderr="sensitive-stderr")
        with patch.object(subprocess, "run", side_effect=error):
            with self.assertRaises(SystemExit) as caught:
                renderer.op_read("op://test/item/field")
            self.assertNotIn("sensitive", str(caught.exception))

    def test_staging_excludes_existing_header(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory) / "repo"
            stage = Path(directory) / "stage"
            (root / "src").mkdir(parents=True)
            (root / "include").mkdir()
            stage.mkdir()
            (root / "include/secrets.h").write_text("old secret")
            (root / "include/pin_config.h").write_text("pins")
            (root / "src/main.cpp").write_text("source")
            for name in ("platformio.ini", "partitions_ota_no_fs.csv"):
                (root / name).write_text("config")
            deployment.stage_sources(root, stage)
            self.assertFalse((stage / "include/secrets.h").exists())
            self.assertTrue((stage / "include/pin_config.h").exists())

    def test_upload_uses_installed_helper_in_process(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            helper = root / "packages/framework-arduinoespressif32/tools/espota.py"
            helper.parent.mkdir(parents=True)
            helper.write_text('def main(args):\n assert args[args.index("-a") + 1] == "test-ota-key"\n assert args[args.index("-P") + 1] == "3232"\n assert "--debug" not in args\n return 0\n')
            with patch.dict(os.environ, {"PLATFORMIO_CORE_DIR": str(root), "ALARM_PANEL_OTA_PASSWORD": "test-ota-key"}), patch.object(subprocess, "run", side_effect=AssertionError("no secret subprocess arguments")):
                deployment.upload(root / "firmware.bin", "192.168.2.100", "192.168.2.10", 3232)


if __name__ == "__main__":
    unittest.main()
