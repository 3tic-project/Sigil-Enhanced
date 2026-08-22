#!/usr/bin/env python3

import json
import pathlib
import subprocess
import sys


app = pathlib.Path(sys.argv[1]).resolve()
runtime = app / "Contents" / "python-runtime" / "bin" / "python3"
python_root = app / "Contents" / "python3lib"
bootstrap = python_root / "sigil_kfx_import" / "bootstrap.py"

if not runtime.is_file():
    raise AssertionError("Debug app has no internal Python runtime entry: {0}".format(runtime))
if not bootstrap.is_file():
    raise AssertionError("Debug app has no KFX bootstrap: {0}".format(bootstrap))

process = subprocess.run(
    [str(runtime), "-I", "-S", str(bootstrap), "--probe-imports"],
    check=False,
    stdout=subprocess.PIPE,
    stderr=subprocess.PIPE,
    text=True,
)
if process.returncode != 0:
    raise AssertionError(
        "KFX worker import probe failed:\n{0}".format(process.stderr or process.stdout)
    )
loaded = json.loads(process.stdout)
for name in ("PIL", "PIL.Image", "lxml", "lxml.etree", "bs4"):
    if name not in loaded:
        raise AssertionError("KFX worker probe missed {0}".format(name))
