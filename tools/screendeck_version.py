#!/usr/bin/env python3
"""Resolve ScreenDeck version metadata and stamp firmware/site files."""

from __future__ import annotations

import argparse
import json
import os
from dataclasses import dataclass
from datetime import datetime, timezone
from pathlib import Path
import subprocess
import sys
from typing import Dict, Optional, Sequence


ROOT = Path(__file__).resolve().parents[1]
RELEASE_JSON = ROOT / "site" / "firmware" / "release.json"
DEVICES_JSON = ROOT / "site" / "firmware" / "devices.json"
MANIFEST_JSON = (
    ROOT / "site" / "firmware" / "esp32-s3-4848s040" / "manifest.json"
)
GENERATED_HEADER = ROOT / "include" / "screendeck_version_generated.h"
STABLE_FACTORY_NAME = "screendeck-esp32s3-4848s040-factory.bin"


@dataclass(frozen=True)
class VersionInfo:
    version: str
    channel: str
    built_at: str
    built_on: str
    tag: Optional[str]
    factory_image: str

    def to_json(self) -> Dict[str, object]:
        return {
            "schemaVersion": 1,
            "channel": self.channel,
            "version": self.version,
            "tag": self.tag,
            "builtAt": self.built_at,
            "builtOn": self.built_on,
            "factoryImage": self.factory_image,
        }


def utc_now() -> str:
    return datetime.now(timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")


def git_output(args: Sequence[str]) -> Optional[str]:
    try:
        result = subprocess.run(
            ["git", *args],
            cwd=ROOT,
            check=True,
            capture_output=True,
            text=True,
        )
    except (OSError, subprocess.CalledProcessError):
        return None
    value = result.stdout.strip()
    return value or None


def tag_from_github_ref() -> Optional[str]:
    ref = os.environ.get("GITHUB_REF", "")
    if ref.startswith("refs/tags/"):
        return ref.removeprefix("refs/tags/")
    name = os.environ.get("GITHUB_REF_NAME", "")
    ref_type = os.environ.get("GITHUB_REF_TYPE", "")
    if ref_type == "tag" and name:
        return name
    return None


def sanitize_filename(version: str) -> str:
    return "".join(
        char if char.isalnum() or char in "._-" else "-"
        for char in version
    ).strip("-") or "development"


def factory_image_for(version: str, channel: str) -> str:
    if channel == "release":
        return f"./screendeck-esp32s3-4848s040-{sanitize_filename(version)}-factory.bin"
    return f"./{STABLE_FACTORY_NAME}"


def resolve(
    version: Optional[str] = None,
    channel: Optional[str] = None,
    built_at: Optional[str] = None,
    tag: Optional[str] = None,
) -> VersionInfo:
    env_version = os.environ.get("SCREENDECK_VERSION", "").strip()
    env_channel = os.environ.get("SCREENDECK_CHANNEL", "").strip()
    env_built_at = os.environ.get("SCREENDECK_BUILD_TIME", "").strip()
    env_tag = os.environ.get("SCREENDECK_TAG", "").strip()

    tag = tag or env_tag or tag_from_github_ref()
    channel = channel or env_channel or ("release" if tag else "development")
    version = version or env_version or tag
    if not version:
        described = git_output(
            ["describe", "--tags", "--always", "--dirty", "--match", "v*"]
        )
        version = described or "development"
        if described and not described.startswith("v") and channel == "development":
            version = f"dev-{described}"

    built_at = built_at or env_built_at or utc_now()
    built_on = built_at[:10] if len(built_at) >= 10 else built_at
    if channel != "release":
        tag = None if not tag else tag

    return VersionInfo(
        version=version,
        channel=channel,
        built_at=built_at,
        built_on=built_on,
        tag=tag if channel == "release" else None,
        factory_image=factory_image_for(version, channel),
    )


def info_from_json(data: Dict[str, object]) -> VersionInfo:
    version = str(data.get("version") or "development")
    channel = str(data.get("channel") or "development")
    built_at = str(data.get("builtAt") or utc_now())
    built_on = str(data.get("builtOn") or built_at[:10])
    raw_tag = data.get("tag")
    tag = str(raw_tag) if raw_tag else None
    factory = str(data.get("factoryImage") or factory_image_for(version, channel))
    return VersionInfo(
        version=version,
        channel=channel,
        built_at=built_at,
        built_on=built_on,
        tag=tag,
        factory_image=factory,
    )


def write_json(path: Path, payload: Dict[str, object]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(payload, indent=2) + "\n", encoding="utf-8")


def apply_to_site(info: VersionInfo, root: Path = ROOT) -> None:
    release_path = root / "site" / "firmware" / "release.json"
    devices_path = root / "site" / "firmware" / "devices.json"
    manifest_path = (
        root / "site" / "firmware" / "esp32-s3-4848s040" / "manifest.json"
    )

    write_json(release_path, info.to_json())

    devices = json.loads(devices_path.read_text(encoding="utf-8"))
    if devices.get("devices"):
        device = devices["devices"][0]
        device["status"] = info.channel
        device["version"] = info.version
        device["builtAt"] = info.built_at
        device["builtOn"] = info.built_on
    write_json(devices_path, devices)

    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    manifest["version"] = info.version
    builds = manifest.get("builds") or []
    if builds and builds[0].get("parts"):
        builds[0]["parts"][0]["path"] = info.factory_image
    write_json(manifest_path, manifest)


def write_header(info: VersionInfo, path: Path = GENERATED_HEADER) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(
        "#pragma once\n"
        f'#define SCREENDECK_VERSION "{info.version}"\n'
        f'#define SCREENDECK_BUILD_TIME "{info.built_at}"\n'
        f'#define SCREENDECK_CHANNEL "{info.channel}"\n',
        encoding="utf-8",
    )


def load_site_info() -> VersionInfo:
    if RELEASE_JSON.is_file():
        try:
            return info_from_json(json.loads(RELEASE_JSON.read_text(encoding="utf-8")))
        except (OSError, json.JSONDecodeError, TypeError, ValueError):
            pass
    return resolve()


def copy_factory_image(info: VersionInfo, source: Optional[Path] = None) -> Optional[Path]:
    firmware_dir = ROOT / "site" / "firmware" / "esp32-s3-4848s040"
    stable = firmware_dir / STABLE_FACTORY_NAME
    source = source or (stable if stable.is_file() else None)
    if source is None or not source.is_file():
        return None

    destination = firmware_dir / Path(info.factory_image).name
    if source.resolve() != destination.resolve():
        destination.write_bytes(source.read_bytes())
    return destination


def main(argv: Optional[Sequence[str]] = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "commands",
        nargs="+",
        choices=("resolve", "stamp-site", "write-header", "apply-json", "copy-factory"),
    )
    parser.add_argument("--json", dest="json_path", help="release.json to apply")
    args = parser.parse_args(argv)

    info = resolve()
    if "apply-json" in args.commands:
        json_path = Path(args.json_path or RELEASE_JSON)
        info = info_from_json(json.loads(json_path.read_text(encoding="utf-8")))
    elif "copy-factory" in args.commands and "stamp-site" not in args.commands:
        info = load_site_info()

    for command in args.commands:
        if command == "resolve":
            json.dump(info.to_json(), sys.stdout, indent=2)
            sys.stdout.write("\n")
        elif command == "stamp-site":
            apply_to_site(info)
        elif command == "write-header":
            write_header(info)
        elif command == "apply-json":
            apply_to_site(info)
        elif command == "copy-factory":
            copied = copy_factory_image(info)
            if copied is None:
                print("factory image is missing; run merge-web-firmware.sh first", file=sys.stderr)
                return 1
            print(copied)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
