#!/usr/bin/env python3
"""Guards for firmware version stamping and tagged GitHub releases."""

from __future__ import annotations

import importlib.util
import json
import os
from pathlib import Path
import shutil
import sys
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]
MAIN = (ROOT / "src" / "main.cpp").read_text()
WEB = (ROOT / "src" / "web_portal.cpp").read_text()
DISPLAY = (ROOT / "src" / "display_ui.cpp").read_text()
HEADER = (ROOT / "include" / "screendeck_version.h").read_text()
UI_FONT = (ROOT / "src" / "ui_font_16_zh.c").read_text()
GITIGNORE = (ROOT / ".gitignore").read_text()
PIO = (ROOT / "platformio.ini").read_text()
RELEASE_WORKFLOW = (ROOT / ".github" / "workflows" / "release.yml").read_text()
MERGE = (ROOT / "tools" / "merge-web-firmware.sh").read_text()


def function_body(source: str, signature: str) -> str:
    signature_at = source.index(signature)
    body_at = source.index("{", signature_at)
    depth = 0
    for index in range(body_at, len(source)):
        if source[index] == "{":
            depth += 1
        elif source[index] == "}":
            depth -= 1
            if depth == 0:
                return source[body_at + 1 : index]
    raise AssertionError("unterminated function: " + signature)


def load_version_module():
    spec = importlib.util.spec_from_file_location(
        "screendeck_version", ROOT / "tools" / "screendeck_version.py"
    )
    module = importlib.util.module_from_spec(spec)
    assert spec.loader is not None
    sys.modules[spec.name] = module
    spec.loader.exec_module(module)
    return module


class FirmwareVersionGuards(unittest.TestCase):
    def test_firmware_compiles_in_version_and_build_time(self) -> None:
        self.assertIn('#include "screendeck_version.h"', MAIN)
        self.assertIn("SCREENDECK_VERSION", MAIN)
        self.assertIn("SCREENDECK_BUILD_TIME", MAIN)
        self.assertIn("SCREENDECK_CHANNEL", MAIN)
        self.assertIn('#include "screendeck_version.h"', WEB)
        self.assertIn('doc["firmwareVersion"]', WEB)
        self.assertIn('doc["firmwareBuildTime"]', WEB)
        self.assertIn('doc["firmwareChannel"]', WEB)

    def test_system_settings_show_firmware_version_and_build_date(self) -> None:
        self.assertIn('#include "screendeck_version.h"', DISPLAY)
        system_settings = function_body(DISPLAY, "void renderSystemSettings(")
        self.assertIn('uiText("固件", "Firmware")', system_settings)
        self.assertIn("SCREENDECK_VERSION", system_settings)
        self.assertIn("SCREENDECK_BUILD_TIME", system_settings)
        self.assertIn('" / "', system_settings)
        self.assertNotIn('" · "', system_settings[system_settings.index("firmwarePanel") :])
        self.assertIn('U+56FA "固"', UI_FONT)
        self.assertIn('U+4EF6 "件"', UI_FONT)
        self.assertGreater(
            system_settings.index("firmwarePanel"),
            system_settings.index("networkPanel"),
        )
        self.assertGreater(
            system_settings.index("SettingsAction::OpenWifiReset"),
            system_settings.index("firmwarePanel"),
        )

    def test_generated_header_is_gitignored_and_injected_before_compile(self) -> None:
        self.assertIn("screendeck_version_generated.h", HEADER)
        self.assertIn('#define SCREENDECK_VERSION "development"', HEADER)
        self.assertIn('#define SCREENDECK_BUILD_TIME "unknown"', HEADER)
        self.assertIn("include/screendeck_version_generated.h", GITIGNORE)
        self.assertIn("pre:tools/inject_version.py", PIO)

    def test_resolve_uses_explicit_environment(self) -> None:
        module = load_version_module()
        previous = {
            key: os.environ.get(key)
            for key in (
                "SCREENDECK_VERSION",
                "SCREENDECK_CHANNEL",
                "SCREENDECK_BUILD_TIME",
                "SCREENDECK_TAG",
                "GITHUB_REF",
                "GITHUB_REF_NAME",
                "GITHUB_REF_TYPE",
            )
        }
        try:
            os.environ["SCREENDECK_VERSION"] = "v1.2.3"
            os.environ["SCREENDECK_CHANNEL"] = "release"
            os.environ["SCREENDECK_BUILD_TIME"] = "2026-08-22T09:30:00Z"
            os.environ["SCREENDECK_TAG"] = "v1.2.3"
            os.environ.pop("GITHUB_REF", None)
            os.environ.pop("GITHUB_REF_NAME", None)
            os.environ.pop("GITHUB_REF_TYPE", None)
            info = module.resolve()
        finally:
            for key, value in previous.items():
                if value is None:
                    os.environ.pop(key, None)
                else:
                    os.environ[key] = value

        self.assertEqual(info.version, "v1.2.3")
        self.assertEqual(info.channel, "release")
        self.assertEqual(info.built_at, "2026-08-22T09:30:00Z")
        self.assertEqual(info.built_on, "2026-08-22")
        self.assertEqual(info.tag, "v1.2.3")
        self.assertEqual(
            info.factory_image,
            "./screendeck-esp32s3-4848s040-v1.2.3-factory.bin",
        )

    def test_stamp_writes_release_metadata_without_touching_the_repo_tree(self) -> None:
        module = load_version_module()
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            firmware_dir = root / "site" / "firmware" / "esp32-s3-4848s040"
            firmware_dir.mkdir(parents=True)
            shutil.copy(
                ROOT / "site" / "firmware" / "devices.json",
                root / "site" / "firmware" / "devices.json",
            )
            shutil.copy(
                ROOT / "site" / "firmware" / "esp32-s3-4848s040" / "manifest.json",
                firmware_dir / "manifest.json",
            )
            info = module.VersionInfo(
                version="v1.2.3",
                channel="release",
                built_at="2026-08-22T09:30:00Z",
                built_on="2026-08-22",
                tag="v1.2.3",
                factory_image="./screendeck-esp32s3-4848s040-v1.2.3-factory.bin",
            )
            module.apply_to_site(info, root)

            release = json.loads(
                (root / "site" / "firmware" / "release.json").read_text()
            )
            devices = json.loads(
                (root / "site" / "firmware" / "devices.json").read_text()
            )
            manifest = json.loads((firmware_dir / "manifest.json").read_text())
            self.assertEqual(release["version"], "v1.2.3")
            self.assertEqual(release["channel"], "release")
            self.assertEqual(release["builtOn"], "2026-08-22")
            self.assertEqual(devices["devices"][0]["status"], "release")
            self.assertEqual(devices["devices"][0]["version"], "v1.2.3")
            self.assertEqual(manifest["version"], "v1.2.3")
            self.assertEqual(
                manifest["builds"][0]["parts"][0]["path"],
                "./screendeck-esp32s3-4848s040-v1.2.3-factory.bin",
            )

    def test_tag_workflow_stamps_builds_releases_and_deploys_pages(self) -> None:
        for fragment in (
            'tags:',
            '"v*"',
            "SCREENDECK_VERSION",
            "SCREENDECK_BUILD_TIME",
            "stamp-site",
            "write-header",
            "pio run",
            "merge-web-firmware.sh",
            "gh release create",
            "deploy-pages",
        ):
            self.assertIn(fragment, RELEASE_WORKFLOW)
        self.assertIn("contents: write", RELEASE_WORKFLOW)
        self.assertNotIn("branches:", RELEASE_WORKFLOW)
        self.assertFalse(
            (ROOT / ".github" / "workflows" / "pages.yml").exists(),
            "Pages must ship with the tagged release workflow, not a main-branch deploy",
        )

    def test_merge_script_writes_a_dio_factory_image(self) -> None:
        self.assertIn("--flash-mode dio", MERGE)
        self.assertIn("--flash-freq 80m", MERGE)
        self.assertIn("--flash-size 16MB", MERGE)


if __name__ == "__main__":
    unittest.main(verbosity=2)
