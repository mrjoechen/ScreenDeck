#!/usr/bin/env python3
"""Convert ShowcaseApp Android weather vectors into embedded LVGL PNGs."""

from __future__ import annotations

import argparse
import html
import os
import shutil
import subprocess
import tempfile
import xml.etree.ElementTree as ET
from pathlib import Path


ANDROID = "{http://schemas.android.com/apk/res/android}"


def android_value(element: ET.Element, name: str, default: str = "") -> str:
    return element.attrib.get(ANDROID + name, default)


def vector_to_svg(source: Path) -> str:
    vector = ET.parse(source).getroot()
    width = android_value(vector, "viewportWidth", "48")
    height = android_value(vector, "viewportHeight", "48")
    paths: list[str] = []
    for item in vector:
        if item.tag.rsplit("}", 1)[-1] != "path":
            raise ValueError(f"unsupported vector item in {source}: {item.tag}")
        attributes = {
            "d": android_value(item, "pathData"),
            "fill": android_value(item, "fillColor", "none"),
        }
        stroke = android_value(item, "strokeColor")
        if stroke:
            attributes["stroke"] = stroke
            attributes["stroke-width"] = android_value(item, "strokeWidth", "1")
        fill_alpha = android_value(item, "fillAlpha")
        if fill_alpha:
            attributes["fill-opacity"] = fill_alpha
        if android_value(item, "fillType") == "evenOdd":
            attributes["fill-rule"] = "evenodd"
        rendered = " ".join(
            f'{name}="{html.escape(value, quote=True)}"'
            for name, value in attributes.items()
        )
        paths.append(f"  <path {rendered}/>")
    return (
        '<svg xmlns="http://www.w3.org/2000/svg" width="48" height="48" '
        f'viewBox="0 0 {width} {height}">\n'
        + "\n".join(paths)
        + "\n</svg>\n"
    )


def render_png(source: Path, target: Path) -> None:
    target.with_suffix(".svg").write_text(vector_to_svg(source))
    svg = target.with_suffix(".svg")
    node_modules = os.environ.get("CODEX_NODE_MODULES")
    if not node_modules:
        candidates = sorted(
            Path.home().glob(
                ".cache/codex-runtimes/codex-primary-runtime/"
                "dependencies/node/node_modules"
            )
        )
        node_modules = str(candidates[-1]) if candidates else ""
    node = shutil.which("node")
    if not node or not node_modules or not (Path(node_modules) / "sharp").exists():
        raise RuntimeError(
            "SVG rendering requires Node.js and sharp; set CODEX_NODE_MODULES "
            "to a node_modules directory containing sharp"
        )
    environment = os.environ.copy()
    environment["NODE_PATH"] = node_modules
    subprocess.run(
        [
            node,
            "-e",
            (
                'require("sharp")(process.argv[1]).resize(48,48,{fit:"fill"})'
                '.png().toFile(process.argv[2]).catch(error=>{'
                "console.error(error);process.exit(1)})"
            ),
            str(svg),
            str(target),
        ],
        check=True,
        env=environment,
    )


def byte_lines(data: bytes) -> str:
    lines = []
    for start in range(0, len(data), 16):
        chunk = data[start : start + 16]
        lines.append("    " + ", ".join(f"0x{value:02x}" for value in chunk) + ",")
    return "\n".join(lines)


def generate(source_dir: Path, project_root: Path) -> None:
    icons = sorted(source_dir.glob("ic_weather_*.xml"))
    if not icons:
        raise SystemExit(f"no weather icons found in {source_dir}")

    header_lines = [
        "#pragma once",
        "",
        "#include <lvgl.h>",
        "",
        "#ifdef __cplusplus",
        'extern "C" {',
        "#endif",
        "",
    ]
    source_lines = [
        '// Generated from ShowcaseApp composeResources/drawable by',
        '// tools/generate_weather_icons.py. Do not edit by hand.',
        '#include "weather_icons.h"',
        "",
    ]
    with tempfile.TemporaryDirectory(prefix="weather-icons-") as temp:
        temp_dir = Path(temp)
        for icon in icons:
            name = icon.stem
            png = temp_dir / f"{name}.png"
            render_png(icon, png)
            data = png.read_bytes()
            symbol = f"weather_{name.removeprefix('ic_weather_')}"
            header_lines.append(f"extern const lv_img_dsc_t {symbol};")
            source_lines.extend(
                [
                    f"static const uint8_t {symbol}_data[] = {{",
                    byte_lines(data),
                    "};",
                    f"const lv_img_dsc_t {symbol} = {{",
                    "    .header = {.cf = LV_IMG_CF_RAW_ALPHA, .always_zero = 0,",
                    "               .reserved = 0, .w = 48, .h = 48},",
                    f"    .data_size = sizeof({symbol}_data),",
                    f"    .data = {symbol}_data,",
                    "};",
                    "",
                ]
            )

    header_lines.extend(["", "#ifdef __cplusplus", "}", "#endif"])

    (project_root / "include" / "weather_icons.h").write_text(
        "\n".join(header_lines) + "\n"
    )
    (project_root / "src" / "weather_icons.c").write_text(
        "\n".join(source_lines) + "\n"
    )


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("source_dir", type=Path)
    parser.add_argument(
        "--project-root", type=Path, default=Path(__file__).resolve().parents[1]
    )
    args = parser.parse_args()
    generate(args.source_dir, args.project_root)


if __name__ == "__main__":
    main()
