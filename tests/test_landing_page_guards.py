#!/usr/bin/env python3
"""Release guards for the static GitHub Pages firmware installer."""

from __future__ import annotations

from html.parser import HTMLParser
import json
from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[1]
SITE = ROOT / "site"
INDEX = (SITE / "index.html").read_text()
SITE_JS = (SITE / "assets" / "site.js").read_text()
WORKFLOW = (ROOT / ".github" / "workflows" / "release.yml").read_text()
MERGE = (ROOT / "tools" / "merge-web-firmware.sh").read_text()


class HeadingParser(HTMLParser):
    def __init__(self) -> None:
        super().__init__()
        self.h1_count = 0

    def handle_starttag(self, tag: str, attrs: list[tuple[str, str | None]]) -> None:
        if tag == "h1":
            self.h1_count += 1


class LandingPageGuards(unittest.TestCase):
    def test_root_stylesheet_forwards_to_the_canonical_tokens(self) -> None:
        self.assertEqual(
            (ROOT / "tokens.css").read_text().strip(),
            '/* Canonical ScreenDeck design tokens live with the deployable site. */\n'
            '@import url("./site/assets/tokens.css");',
        )

    def test_page_is_portable_to_a_github_project_page(self) -> None:
        self.assertNotRegex(INDEX, r'(?:src|href)="/(?!/)')
        self.assertIn("esp-web-tools@10.4.0", INDEX)
        self.assertIn('<esp-web-install-button id="webInstaller">', INDEX)

        parser = HeadingParser()
        parser.feed(INDEX)
        self.assertEqual(parser.h1_count, 1)

    def test_navigation_exposes_github_language_and_support_controls(self) -> None:
        for fragment in (
            'id="githubLink"',
            'href="https://github.com/mrjoechen/ScreenDeck"',
            'id="languageToggle"',
            'id="donateLink"',
            'id="deviceLink"',
            'href="http://screendeck.local/"',
            'src="./assets/kofi-logo.webp"',
            'data-i18n="heroCta">立即刷入',
            'data-i18n="deviceCta">打开设备',
        ):
            self.assertIn(fragment, INDEX)

        self.assertNotIn("打开刷机台", INDEX)
        self.assertNotIn('nav-edge__action" href="#install"', INDEX)
        self.assertTrue((SITE / "assets" / "kofi-logo.webp").is_file())

    def test_site_is_ready_for_github_pages_sharing(self) -> None:
        self.assertIn('content="https://mrjoechen.github.io/ScreenDeck/"', INDEX)
        self.assertIn(
            'content="https://mrjoechen.github.io/ScreenDeck/assets/device_preview_01.JPG"',
            INDEX,
        )
        self.assertTrue((SITE / ".nojekyll").is_file())
        self.assertTrue((SITE / "robots.txt").is_file())
        self.assertTrue((SITE / "404.html").is_file())
        self.assertTrue((SITE / "assets" / "device_preview_01.JPG").is_file())

    def test_hero_uses_hardware_proof_without_a_preview_pager(self) -> None:
        self.assertIn('class="hero__proof reveal"', INDEX)
        self.assertIn('aria-label="当前硬件配置"', INDEX)
        self.assertIn('<dl class="signal-table">', INDEX)
        self.assertNotIn("data-screen-pager", INDEX)
        self.assertNotIn("firmware-screens/", INDEX)

    def test_registry_points_to_a_model_specific_manifest(self) -> None:
        registry = json.loads((SITE / "firmware" / "devices.json").read_text())
        self.assertEqual(len(registry["devices"]), 1)

        device = registry["devices"][0]
        self.assertEqual(device["id"], "esp32-s3-4848s040")
        self.assertEqual(device["status"], "development")
        self.assertEqual(device["version"], "development")
        self.assertTrue(device["manifest"].startswith("./"))

        release = json.loads((SITE / "firmware" / "release.json").read_text())
        self.assertEqual(release["schemaVersion"], 1)
        self.assertEqual(release["channel"], "development")
        self.assertEqual(release["version"], "development")
        self.assertTrue(str(release["factoryImage"]).endswith("factory.bin"))

        manifest_path = SITE / device["manifest"].removeprefix("./")
        self.assertTrue(manifest_path.is_file())

    def test_manifest_is_an_erasable_s3_factory_image(self) -> None:
        manifest = json.loads(
            (
                SITE
                / "firmware"
                / "esp32-s3-4848s040"
                / "manifest.json"
            ).read_text()
        )
        self.assertEqual(manifest["version"], "development")
        self.assertTrue(manifest["new_install_prompt_erase"])
        self.assertEqual(len(manifest["builds"]), 1)

        build = manifest["builds"][0]
        self.assertEqual(build["chipFamily"], "ESP32-S3")
        self.assertEqual(len(build["parts"]), 1)
        self.assertEqual(build["parts"][0]["offset"], 0)
        self.assertRegex(build["parts"][0]["path"], r"factory\.bin$")
        self.assertNotIn("dist/", json.dumps(manifest))

    def test_install_panel_exposes_firmware_version_and_update_date(self) -> None:
        for fragment in (
            'id="releaseChannel"',
            'id="releaseVersion"',
            'id="releaseUpdated"',
            'data-i18n="versionLabel"',
            'data-i18n="updatedLabel"',
        ):
            self.assertIn(fragment, INDEX)
        self.assertIn("../firmware/release.json", SITE_JS)
        self.assertIn("updateReleaseLine", SITE_JS)
        self.assertIn("channelRelease", SITE_JS)

    def test_deploy_workflow_rebuilds_and_merges_the_current_firmware(self) -> None:
        self.assertIn('tags:', WORKFLOW)
        self.assertIn('"v*"', WORKFLOW)
        self.assertNotIn("branches:", WORKFLOW)
        for fragment in (
            "pio run",
            "merge-web-firmware.sh",
            "gh release create",
            "deploy-pages",
        ):
            self.assertIn(fragment, WORKFLOW)
        for fragment in (
            "merge-bin",
            "--flash-mode dio",
            "--flash-freq 80m",
            "--flash-size 16MB",
            "0x0",
            "0x8000",
            "0x10000",
            "image-info",
        ):
            self.assertIn(fragment, MERGE)
        self.assertNotIn("dist/", WORKFLOW)


if __name__ == "__main__":
    unittest.main(verbosity=2)
