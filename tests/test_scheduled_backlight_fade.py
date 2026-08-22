#!/usr/bin/env python3
"""Host-side behavior tests for the scheduled screen-off fade."""

from pathlib import Path
import subprocess
import tempfile
import textwrap
import unittest


ROOT = Path(__file__).resolve().parents[1]


class ScheduledBacklightFadeTests(unittest.TestCase):
    def compile_and_run(self, assertions: str) -> None:
        source = textwrap.dedent(
            f"""
            #include <cassert>
            #include <cstdint>

            #include "backlight_fade.h"

            int main() {{
            {textwrap.indent(textwrap.dedent(assertions), "  ")}
              return 0;
            }}
            """
        )
        with tempfile.TemporaryDirectory() as directory:
            executable = Path(directory) / "scheduled-backlight-fade"
            compiled = subprocess.run(
                [
                    "c++",
                    "-std=c++17",
                    "-Wall",
                    "-Wextra",
                    "-Werror",
                    "-I",
                    str(ROOT / "include"),
                    "-x",
                    "c++",
                    "-",
                    "-o",
                    str(executable),
                ],
                input=source,
                text=True,
                capture_output=True,
            )
            self.assertEqual(compiled.returncode, 0, compiled.stderr)
            executed = subprocess.run([str(executable)], capture_output=True, text=True)
            self.assertEqual(executed.returncode, 0, executed.stderr)

    def test_fade_uses_a_600ms_eased_curve(self) -> None:
        self.compile_and_run(
            r"""
            ScheduledBacklightFade fade;
            assert(fade.effectivePercent(false, 37) == 37);
            fade.begin(1000, 100);

            assert(fade.active());
            assert(fade.percent() == 100);
            assert(fade.effectivePercent(true, 37) == 100);
            assert(!fade.update(1006));
            assert(fade.percent() == 100);

            assert(fade.update(1150));
            assert(fade.percent() == 42);
            assert(fade.effectivePercent(true, 91) == 42);
            assert(fade.update(1300));
            assert(fade.percent() == 12);
            assert(fade.update(1450));
            assert(fade.percent() == 2);
            assert(fade.active());

            assert(fade.update(1599));
            assert(fade.percent() == 1);
            assert(fade.active());

            assert(fade.update(1600));
            assert(fade.percent() == 0);
            assert(!fade.active());
            """
        )

    def test_fade_starts_at_the_visible_level_and_cancel_restores_full_level(self) -> None:
        self.compile_and_run(
            r"""
            ScheduledBacklightFade fade;
            fade.begin(4000, 60);
            assert(fade.percent() == 60);

            assert(fade.update(4150));
            assert(fade.percent() == 25);
            assert(fade.update(4300));
            assert(fade.percent() == 7);

            fade.cancel();
            assert(!fade.active());
            assert(fade.percent() == 100);
            assert(fade.effectivePercent(false, 37) == 37);
            assert(!fade.update(5000));
            assert(fade.percent() == 100);
            """
        )


if __name__ == "__main__":
    unittest.main(verbosity=2)
