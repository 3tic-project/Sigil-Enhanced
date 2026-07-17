#!/usr/bin/env python3
"""Verify pinned distributions and imports from isolated package directories."""

import argparse
import importlib
from importlib import metadata
import pathlib
import re
import sys


IMPORT_NAMES = {
    "css-parser": "css_parser",
    "httpx-sse": "httpx_sse",
    "jsonschema-specifications": "jsonschema_specifications",
    "pillow": "PIL",
    "pydantic-core": "pydantic_core",
    "pydantic-settings": "pydantic_settings",
    "pyjwt": "jwt",
    "python-dotenv": "dotenv",
    "python-multipart": "python_multipart",
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


def verify(requirements_path, package_directories, extra_imports=()):
    roots = [pathlib.Path(path).resolve() for path in package_directories]
    missing_roots = [str(path) for path in roots if not path.is_dir()]
    if missing_roots:
        raise RuntimeError("package directories do not exist: {0}".format(missing_roots))

    for root in reversed(roots):
        sys.path.insert(0, str(root))
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
        module = importlib.import_module(module_name)
        module_path = getattr(module, "__file__", None)
        if not module_path or not _inside(module_path, roots):
            raise RuntimeError(
                "module {0} was not imported from the package directories".format(module_name)
            )

    for module_name in extra_imports:
        module = importlib.import_module(module_name)
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
