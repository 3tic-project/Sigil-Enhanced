#!/usr/bin/env python3
"""Build a deterministic ZIP accepted by Sigil's plugin manager."""

import argparse
import os
from pathlib import Path
import zipfile


FIXED_TIMESTAMP = (2020, 1, 1, 0, 0, 0)
SKIPPED_DIRECTORIES = {"__MACOSX", "__pycache__"}
SKIPPED_FILENAMES = {".DS_Store", "Thumbs.db"}
SKIPPED_SUFFIXES = {".pyc", ".pyo", ".zip"}


def plugin_files(plugin_dir):
    """Yield source path and installer-relative archive path pairs."""
    for source in sorted(plugin_dir.rglob("*")):
        relative = source.relative_to(plugin_dir)
        if source.is_symlink():
            raise ValueError("plugin packages must not contain symlinks: {0}".format(relative))
        if not source.is_file():
            continue
        if any(part.startswith(".") for part in relative.parts):
            continue
        if any(part in SKIPPED_DIRECTORIES for part in relative.parts):
            continue
        if source.name in SKIPPED_FILENAMES or source.suffix.lower() in SKIPPED_SUFFIXES:
            continue
        yield source, Path(plugin_dir.name) / relative


def package_plugin(plugin_dir, destination=None):
    plugin_dir = Path(plugin_dir).resolve()
    if not plugin_dir.is_dir():
        raise ValueError("plugin directory does not exist: {0}".format(plugin_dir))
    for required in ("plugin.xml", "plugin.py"):
        if not (plugin_dir / required).is_file():
            raise ValueError("plugin directory is missing {0}".format(required))

    default_destination = plugin_dir.parent / (plugin_dir.name + ".zip")
    destination = Path(destination or default_destination).resolve()
    install_name = destination.stem.split("_", 1)[0]
    if install_name != plugin_dir.name:
        raise ValueError(
            "ZIP basename must be {0} or {0}_<version>".format(plugin_dir.name)
        )
    destination.parent.mkdir(parents=True, exist_ok=True)
    temporary = destination.with_suffix(destination.suffix + ".tmp")

    try:
        with zipfile.ZipFile(temporary, "w", compression=zipfile.ZIP_DEFLATED) as archive:
            for source, archive_path in plugin_files(plugin_dir):
                info = zipfile.ZipInfo(archive_path.as_posix(), FIXED_TIMESTAMP)
                info.compress_type = zipfile.ZIP_DEFLATED
                info.create_system = 3
                info.external_attr = 0o100644 << 16
                archive.writestr(info, source.read_bytes())
        os.replace(temporary, destination)
    finally:
        if temporary.exists():
            temporary.unlink()
    return destination


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("plugin_dir", type=Path)
    parser.add_argument("destination", nargs="?", type=Path)
    args = parser.parse_args()
    try:
        print(package_plugin(args.plugin_dir, args.destination))
    except ValueError as error:
        parser.error(str(error))


if __name__ == "__main__":
    main()
