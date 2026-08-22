"""Stamp SCREENDECK_* macros into a generated header before firmware compile."""

from pathlib import Path
import importlib.util
import sys


Import("env")  # noqa: F821

PROJECT_DIR = Path(env.subst("$PROJECT_DIR"))  # noqa: F821
MODULE_PATH = PROJECT_DIR / "tools" / "screendeck_version.py"

spec = importlib.util.spec_from_file_location("screendeck_version", MODULE_PATH)
module = importlib.util.module_from_spec(spec)
assert spec.loader is not None
sys.modules[spec.name] = module
spec.loader.exec_module(module)

info = module.resolve()
module.write_header(info, PROJECT_DIR / "include" / "screendeck_version_generated.h")
print(
    "ScreenDeck %s (%s) built %s" % (info.version, info.channel, info.built_at)
)
