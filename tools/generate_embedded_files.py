"""Generate the embedded-data assembly stubs that SCons expects but never runs.

ESP-IDF components embed binary/text payloads with `target_add_binary_data()`.
CMake turns each one into a ninja CUSTOM_COMMAND that produces a `.S` file in
the build directory. PlatformIO does not build with ninja -- it re-expresses the
ninja graph as SCons nodes -- and those custom commands are dropped along the
way, so the `.S` files are treated as pre-existing sources. They survive by
accident from earlier builds and disappear whenever CMake reconfigures, which
fails the build with "Source `.../<name>.S' not found".

This runs the same commands CMake recorded, for every embed rule that has no
output yet. Components affected here come in through Arduino's dependency tree
(esp_insights, esp_rainmaker).
"""

import re
import subprocess
from pathlib import Path

Import("env")  # noqa: F821

BUILD_DIR = Path(env.subst("$BUILD_DIR"))  # noqa: F821
NINJA_FILE = BUILD_DIR / "build.ninja"

# `build <name>.S | ${cmake_ninja_workdir}<name>.S: CUSTOM_COMMAND ...`
# followed by an indented `COMMAND = ...` line.
EMBED_RULE = re.compile(
    r"^build (?P<output>[^\s|:]+\.S) \| [^\n]*: CUSTOM_COMMAND[^\n]*\n"
    r"(?:^  [A-Z_]+ = [^\n]*\n)*?"
    r"^  COMMAND = (?P<command>[^\n]*)$",
    re.MULTILINE,
)


def generate_embedded_files(*_args, **_kwargs):
    if not NINJA_FILE.is_file():
        return

    manifest = NINJA_FILE.read_text(encoding="utf-8", errors="replace")
    for match in EMBED_RULE.finditer(manifest):
        output = BUILD_DIR / match.group("output")
        if output.is_file():
            continue
        command = match.group("command")
        if "data_file_embed_asm.cmake" not in command:
            continue
        print("Generating embedded data assembly: %s" % output.name)
        result = subprocess.run(command, shell=True, capture_output=True, text=True)
        if result.returncode != 0:
            print(result.stdout)
            print(result.stderr)
            env.Exit(1)  # noqa: F821


generate_embedded_files()
