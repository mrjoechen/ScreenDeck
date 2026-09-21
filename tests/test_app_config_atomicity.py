#!/usr/bin/env python3
"""Regression guards for crash-safe persisted device configuration."""

from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[1]
APP_CONFIG_SOURCE = (ROOT / "src" / "app_config.cpp").read_text()


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
    raise AssertionError(f"unterminated function: {signature}")


class AppConfigAtomicityGuards(unittest.TestCase):
    def test_save_replaces_config_only_after_a_complete_temp_write(self) -> None:
        save = function_body(APP_CONFIG_SOURCE, "bool AppConfig::save() const")

        self.assertIn("measureJson(doc)", save)
        self.assertIn("LittleFS.open(CONFIG_TEMP_PATH, FILE_WRITE)", save)
        self.assertNotIn("LittleFS.open(CONFIG_PATH, FILE_WRITE)", save)
        self.assertIn("file.flush()", save)
        self.assertIn("writtenBytes == expectedBytes", save)
        self.assertIn("LittleFS.open(CONFIG_TEMP_PATH, FILE_READ)", save)
        self.assertIn("check.size() == expectedBytes", save)
        self.assertIn("LittleFS.rename(CONFIG_TEMP_PATH, CONFIG_PATH)", save)

        close = save.index("file.close()")
        reopen = save.index("LittleFS.open(CONFIG_TEMP_PATH, FILE_READ)")
        rename = save.index("LittleFS.rename(CONFIG_TEMP_PATH, CONFIG_PATH)")
        self.assertLess(close, reopen)
        self.assertLess(close, rename)


if __name__ == "__main__":
    unittest.main()
