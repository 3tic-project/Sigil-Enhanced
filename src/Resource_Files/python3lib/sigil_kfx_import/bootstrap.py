#!/usr/bin/env python3
"""Start the KFX worker with only Sigil's bundled Python packages.

The worker is launched with ``python -I -S``, so PYTHONPATH, PYTHONHOME, and
site-packages are ignored. Packaged builds keep Pillow/lxml/bs4 next to the
bundled interpreter (Windows ``Lib/site-packages``, macOS framework
site-packages, AppImage ``lib/pythonX.Y/site-packages``), while Sigil modules
live in ``python3lib``. This bootstrap adds those bundled roots explicitly and
never restores user site-packages.
"""

from __future__ import annotations

import json
from pathlib import Path
import site
import sys


REQUIRED_IMPORTS = (
    "PIL",
    "PIL.Image",
    "lxml",
    "lxml.etree",
    "bs4",
)


def python_root() -> Path:
    return Path(__file__).resolve().parent.parent


def interpreter_site_packages() -> list[Path]:
    prefix = Path(getattr(sys, "base_prefix", sys.prefix))
    version = "python{0}.{1}".format(sys.version_info.major, sys.version_info.minor)
    return [
        prefix / "Lib" / "site-packages",
        prefix / "lib" / version / "site-packages",
        prefix / "lib64" / version / "site-packages",
    ]


def _add_root(root: Path) -> None:
    if not root.is_dir():
        return
    resolved = str(root.resolve())
    if resolved not in sys.path:
        site.addsitedir(resolved)


def configure_sys_path() -> Path:
    root = python_root()
    _add_root(root)
    _add_root(root / "sigil_kfx_import" / "kfxlib" / "calibre-plugin-modules")
    for candidate in interpreter_site_packages():
        _add_root(candidate)
    return root


def probe_imports() -> int:
    loaded = {}
    for name in REQUIRED_IMPORTS:
        module = __import__(name, fromlist=["*"] if "." in name else [])
        path = getattr(module, "__file__", None)
        if not path:
            raise ImportError("module {0} has no file path".format(name))
        loaded[name] = path
    sys.stdout.write(json.dumps(loaded, sort_keys=True) + "\n")
    return 0


def main(argv: list[str] | None = None) -> int:
    configure_sys_path()
    args = sys.argv[1:] if argv is None else argv
    if args == ["--probe-imports"]:
        return probe_imports()
    from sigil_kfx_import.worker import main as worker_main

    return worker_main()


if __name__ == "__main__":
    raise SystemExit(main())
