#!/usr/bin/env python3
"""Validate packaged release assets and write deterministic SHA-256 sums."""

import argparse
import hashlib
from pathlib import Path
import sys


CHECKSUM_NAME = "SHA256SUMS.txt"


class ReleaseAssetError(RuntimeError):
    pass


def _sha256(path):
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def prepare_release_assets(directory):
    directory = Path(directory)
    if not directory.is_dir():
        raise ReleaseAssetError("release asset directory does not exist: {0}".format(directory))

    assets = sorted(
        path for path in directory.iterdir()
        if path.is_file() and path.name != CHECKSUM_NAME
    )
    invalid_names = [path.name for path in assets if "\n" in path.name or "\r" in path.name]
    if invalid_names:
        raise ReleaseAssetError("release asset filenames must not contain newlines")

    windows = [path for path in assets if path.suffix.lower() == ".exe"]
    macos = [path for path in assets if path.suffix.lower() == ".dmg"]
    expected = set(windows + macos)
    unexpected = [path.name for path in assets if path not in expected]
    if len(windows) != 1 or len(macos) != 2 or unexpected:
        raise ReleaseAssetError(
            "expected one .exe and two .dmg assets; found {0} .exe, {1} .dmg, "
            "unexpected={2}".format(len(windows), len(macos), unexpected)
        )

    checksum_path = directory / CHECKSUM_NAME
    checksum_path.write_text(
        "".join("{0}  {1}\n".format(_sha256(path), path.name) for path in assets),
        encoding="ascii",
    )
    return checksum_path


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("directory", type=Path)
    args = parser.parse_args(argv)
    try:
        checksum_path = prepare_release_assets(args.directory)
    except ReleaseAssetError as error:
        parser.error(str(error))
    print(checksum_path.read_text(encoding="ascii"), end="")
    return 0


if __name__ == "__main__":
    sys.exit(main())
