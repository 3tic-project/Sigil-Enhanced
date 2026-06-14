#!/usr/bin/env python3

import importlib.util
import pathlib
import shutil
import sys


def copy_path(source, destination):
    if source.is_dir():
        shutil.copytree(source, destination / source.name, dirs_exist_ok=True)
    else:
        destination.mkdir(parents=True, exist_ok=True)
        shutil.copy2(source, destination / source.name)


def main():
    if len(sys.argv) != 3:
        print("usage: copy_python_package.py PACKAGE DESTINATION", file=sys.stderr)
        return 2

    package_name = sys.argv[1]
    destination = pathlib.Path(sys.argv[2])
    destination.mkdir(parents=True, exist_ok=True)

    spec = importlib.util.find_spec(package_name)
    if spec is None:
        print(f"Unable to find required Python package: {package_name}", file=sys.stderr)
        return 1

    if spec.submodule_search_locations:
        package_path = pathlib.Path(next(iter(spec.submodule_search_locations)))
    elif spec.origin:
        package_path = pathlib.Path(spec.origin)
    else:
        print(f"Unable to locate required Python package: {package_name}", file=sys.stderr)
        return 1

    copy_path(package_path, destination)

    site_packages = package_path.parent
    normalized_name = package_name.replace("-", "_")
    for metadata_path in site_packages.glob(f"{normalized_name}-*.dist-info"):
        copy_path(metadata_path, destination)

    return 0


if __name__ == "__main__":
    sys.exit(main())
