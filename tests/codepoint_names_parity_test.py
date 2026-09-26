"""Sample old Python getname results against the C++ Unicode 14 archive."""

import random
import subprocess
import sys
import unicodedata
from pathlib import Path


sys.path.insert(0, str(Path(__file__).resolve().parent / "fixtures"))
from getcodepointname_legacy import getname
from codepoint_name_golden_test import XHTML_CHARS


def legacy_name(cp):
    if cp == -1:
        return "EOF"
    try:
        return getname(cp)
    except ValueError:
        return ""


def main(executable, exhaustive=False):
    if exhaustive:
        samples = [-2, -1, *range(0x110000), 0x110000]
    else:
        samples = [-2, -1, *range(32), 0x4E00, 0xAC00, 0x1F600,
                   0x1FAE8, 0xD800, 0x10FFFF, 0x110000]
        samples.extend(sorted({ord(char) for char in XHTML_CHARS}))
        randomizer = random.Random(20260925)
        samples.extend(randomizer.randrange(0x110000) for _ in range(20000))
    completed = subprocess.run(
        (executable, "--batch-probe"),
        input="".join(f"{cp}\n" for cp in samples),
        text=True, capture_output=True, check=True,
    )
    actual = completed.stdout.splitlines()
    if len(actual) != len(samples):
        raise AssertionError(f"expected {len(samples)} outputs, got {len(actual)}")
    failures = []
    for cp, name in zip(samples, actual):
        expected = legacy_name(cp)
        if name != expected:
            failures.append((cp, expected, name))
    if failures:
        raise AssertionError(f"{len(failures)} mismatches; first ten: {failures[:10]}")
    print(f"matched {len(samples)} legacy codepoint names")


if __name__ == "__main__":
    if unicodedata.unidata_version != "14.0.0":
        print("requires the legacy Python Unicode 14.0.0 database")
        sys.exit(77)
    main(sys.argv[1], len(sys.argv) > 2 and sys.argv[2] == "--exhaustive")
