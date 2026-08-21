#!/usr/bin/env python3
"""Verify pinned distributions and imports from isolated package directories."""

import argparse
import importlib
from importlib import metadata
import pathlib
import re
import site
import sys
from collections import Counter


IMPORT_NAMES = {
    "beautifulsoup4": "bs4",
    "css-parser": "css_parser",
    "httpx-sse": "httpx_sse",
    "jsonschema-specifications": "jsonschema_specifications",
    "pillow": "PIL",
    "pydantic-core": "pydantic_core",
    "pydantic-settings": "pydantic_settings",
    "pyjwt": "jwt",
    "python-dotenv": "dotenv",
    "python-multipart": "python_multipart",
    "pywin32": "pywintypes",
    "rpds-py": "rpds",
    "sse-starlette": "sse_starlette",
    "typing-inspection": "typing_inspection",
    "typing-extensions": "typing_extensions",
}


def normalize_name(value):
    return re.sub(r"[-_.]+", "-", value).lower()


def pinned_requirements(path):
    requirements = {}
    for raw_line in pathlib.Path(path).read_text(encoding="utf-8").splitlines():
        line = raw_line.strip()
        if not line or line.startswith("#"):
            continue
        match = re.fullmatch(r"([A-Za-z0-9_.-]+)==([^\s]+)", line)
        if not match:
            raise RuntimeError("requirement is not exactly pinned: {0}".format(line))
        name, version = match.groups()
        requirements[normalize_name(name)] = version
    return requirements


def _inside(path, roots):
    resolved = pathlib.Path(path).resolve()
    for root in roots:
        try:
            resolved.relative_to(root)
            return True
        except ValueError:
            continue
    return False


def add_isolated_site_directories(roots):
    """Add package roots with normal .pth processing, rejecting path escapes."""
    original_sys_path = list(sys.path)
    before = Counter(
        str(pathlib.Path(path).resolve()) for path in original_sys_path if path
    )
    for root in roots:
        site.addsitedir(str(root))

    after = Counter(str(pathlib.Path(path).resolve()) for path in sys.path if path)
    added = after - before
    escaped = sorted(path for path in added if not _inside(path, roots))
    if escaped:
        sys.path[:] = original_sys_path
        raise RuntimeError(
            "package .pth files added paths outside the package directories: {0}".format(
                escaped
            )
        )


def verify(requirements_path, package_directories, extra_imports=()):
    roots = [pathlib.Path(path).resolve() for path in package_directories]
    missing_roots = [str(path) for path in roots if not path.is_dir()]
    if missing_roots:
        raise RuntimeError("package directories do not exist: {0}".format(missing_roots))

    add_isolated_site_directories(roots)
    distributions = {}
    for distribution in metadata.distributions(path=[str(path) for path in roots]):
        name = distribution.metadata.get("Name")
        if name:
            distributions[normalize_name(name)] = distribution.version

    requirements = pinned_requirements(requirements_path)
    for name, expected_version in requirements.items():
        actual_version = distributions.get(name)
        if actual_version != expected_version:
            raise RuntimeError(
                "distribution {0} expected {1}, found {2}".format(
                    name, expected_version, actual_version
                )
            )
        module_name = IMPORT_NAMES.get(name, name.replace("-", "_"))
        try:
            module = importlib.import_module(module_name)
        except (ImportError, OSError) as error:
            raise RuntimeError(
                "distribution {0} import {1} failed: {2}".format(
                    name, module_name, error
                )
            ) from error
        module_path = getattr(module, "__file__", None)
        if not module_path or not _inside(module_path, roots):
            raise RuntimeError(
                "module {0} was not imported from the package directories".format(module_name)
            )

    for module_name in extra_imports:
        try:
            module = importlib.import_module(module_name)
        except (ImportError, OSError) as error:
            raise RuntimeError(
                "extra import {0} failed: {1}".format(module_name, error)
            ) from error
        module_path = getattr(module, "__file__", None)
        if not module_path or not _inside(module_path, roots):
            raise RuntimeError(
                "extra module {0} was not imported from the package directories".format(
                    module_name
                )
            )
    return len(requirements)


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--requirements", required=True, type=pathlib.Path)
    parser.add_argument(
        "--site-packages", required=True, action="append", type=pathlib.Path
    )
    parser.add_argument("--extra-import", action="append", default=[])
    args = parser.parse_args(argv)
    try:
        count = verify(args.requirements, args.site_packages, args.extra_import)
    except (ImportError, OSError, RuntimeError) as error:
        parser.error(str(error))
    print("Verified {0} pinned distributions and isolated imports.".format(count))
    return 0


if __name__ == "__main__":
    sys.exit(main())
