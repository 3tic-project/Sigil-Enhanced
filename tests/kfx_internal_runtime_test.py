#!/usr/bin/env python3

import json
import pathlib
import subprocess
import sys


app = pathlib.Path(sys.argv[1]).resolve()
runtime = app / "Contents" / "python-runtime" / "bin" / "python3"
python_root = app / "Contents" / "python3lib"

if not runtime.is_file():
    raise AssertionError("Debug app has no internal Python runtime entry: {0}".format(runtime))
if not python_root.is_dir():
    raise AssertionError("Debug app has no internal python3lib: {0}".format(python_root))

probe = r'''import importlib, json, pathlib, sys
root = pathlib.Path(sys.argv[1]).resolve()
sys.path.insert(0, str(root))
sys.path.insert(0, str(root / "sigil_kfx_import" / "kfxlib" / "calibre-plugin-modules"))
paths = {}
for name in ("PIL.Image", "bs4", "lxml.etree", "regex", "sigil_kfx_import.kfxlib"):
    module = importlib.import_module(name)
    path = pathlib.Path(module.__file__).resolve()
    path.relative_to(root)
    paths[name] = str(path)
print(json.dumps(paths, sort_keys=True))
'''
process = subprocess.run(
    [str(runtime), "-I", "-S", "-c", probe, str(python_root)],
    check=False,
    stdout=subprocess.PIPE,
    stderr=subprocess.PIPE,
    text=True,
)
if process.returncode != 0:
    raise AssertionError(
        "internal Python dependency probe failed:\n{0}".format(process.stderr)
    )
loaded = json.loads(process.stdout)
if len(loaded) != 5:
    raise AssertionError("internal Python dependency probe was incomplete")
