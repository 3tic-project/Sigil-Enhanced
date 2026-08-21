#!/usr/bin/env python3
"""Start the KFX worker with only Sigil's internal Python package root."""

from pathlib import Path
import sys


def main() -> int:
    python_root = Path(__file__).resolve().parent.parent
    sys.path.insert(0, str(python_root))
    from sigil_kfx_import.worker import main as worker_main

    return worker_main()


if __name__ == "__main__":
    raise SystemExit(main())
