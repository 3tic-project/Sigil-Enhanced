#!/usr/bin/env python3

import argparse
import hashlib
import json
import os
import pathlib
import platform
import re
import shutil
import site
import subprocess
import sys


SCRIPT_VERSION = 1


def normalize_dist_name(name):
    return re.sub(r"[-_.]+", "-", name).lower()


def requirement_names(requirements_path):
    names = []
    for raw_line in requirements_path.read_text(encoding="utf-8").splitlines():
        line = raw_line.strip()
        if not line or line.startswith("#") or line.startswith("-"):
            continue
        name = re.split(r"\s*(?:==|>=|<=|~=|!=|>|<|\[)", line, maxsplit=1)[0].strip()
        if name:
            names.append(normalize_dist_name(name))
    return names


def runtime_key(requirements_path):
    digest = hashlib.sha256()
    digest.update(requirements_path.read_bytes())
    digest.update(str(SCRIPT_VERSION).encode("ascii"))
    digest.update(sys.version.encode("utf-8"))
    digest.update(sys.executable.encode("utf-8"))
    digest.update(sys.platform.encode("utf-8"))
    digest.update(platform.machine().encode("utf-8"))
    return digest.hexdigest()[:16]


def cache_site_packages(cache_dir, requirements_path):
    key = runtime_key(requirements_path)
    root = cache_dir / f"py{sys.version_info.major}{sys.version_info.minor}-{sys.platform}-{platform.machine()}-{key}"
    site_packages = root / "site-packages"
    marker = root / ".sigil-python-runtime.json"
    expected = {
        "script_version": SCRIPT_VERSION,
        "python": sys.version,
        "executable": sys.executable,
        "requirements_sha256": hashlib.sha256(requirements_path.read_bytes()).hexdigest(),
    }

    if marker.exists() and site_packages.exists():
        try:
            current = json.loads(marker.read_text(encoding="utf-8"))
            if current == expected:
                return site_packages
        except Exception:
            pass

    if root.exists():
        shutil.rmtree(root)
    site_packages.mkdir(parents=True, exist_ok=True)

    subprocess.check_call([
        sys.executable,
        "-m",
        "pip",
        "install",
        "--upgrade",
        "--no-warn-conflicts",
        "--target",
        str(site_packages),
        "-r",
        str(requirements_path),
    ])

    marker.write_text(json.dumps(expected, indent=2, sort_keys=True), encoding="utf-8")
    return site_packages


def copy_path(source, destination):
    destination.mkdir(parents=True, exist_ok=True)
    target = destination / source.name
    if target.exists() or target.is_symlink():
        if target.is_dir() and not target.is_symlink():
            shutil.rmtree(target)
        else:
            target.unlink()
    if source.is_dir():
        shutil.copytree(source, target, ignore=ignore_package_noise)
    else:
        shutil.copy2(source, target)


def ignore_package_noise(base, names):
    ignored = []
    for name in names:
        if name in {"__pycache__", "test", "tests", "testing", "docs", "doc", "demos"}:
            ignored.append(name)
    return ignored


def package_search_roots(cache_site):
    roots = [cache_site]
    for path in site.getsitepackages():
        candidate = pathlib.Path(path)
        if candidate.exists() and candidate not in roots:
            roots.append(candidate)
    user_site = pathlib.Path(site.getusersitepackages())
    if user_site.exists() and user_site not in roots:
        roots.append(user_site)
    for path in sys.path:
        if not path:
            continue
        candidate = pathlib.Path(path)
        if candidate.exists() and candidate.is_dir() and candidate not in roots:
            roots.append(candidate)
    return roots


def find_package(package_name, roots):
    for root in roots:
        candidate = root / package_name
        if candidate.exists():
            return candidate
    return None


def copy_dist_info(requirement_dist_names, roots, destination):
    copied = set()
    for root in roots:
        for metadata_path in list(root.glob("*.dist-info")) + list(root.glob("*.egg-info")):
            dist_name = metadata_path.name.rsplit("-", 1)[0]
            normalized = normalize_dist_name(dist_name)
            if normalized in requirement_dist_names and metadata_path.name not in copied:
                copy_path(metadata_path, destination)
                copied.add(metadata_path.name)


def sync_packages(requirements_path, cache_dir, destination, package_names):
    cache_site = cache_site_packages(cache_dir, requirements_path)
    roots = package_search_roots(cache_site)
    missing = []
    destination.mkdir(parents=True, exist_ok=True)

    for package_name in package_names:
        source = find_package(package_name, roots)
        if source is None and not pathlib.Path(package_name).suffix:
            source = find_package(package_name + ".py", roots)
        if source is None:
            missing.append(package_name)
            continue
        copy_path(source, destination)

    copy_dist_info(set(requirement_names(requirements_path)), roots, destination)

    if missing:
        print("Missing required Python package(s): " + ", ".join(missing), file=sys.stderr)
        return 1
    return 0


def main():
    parser = argparse.ArgumentParser(description="Prepare and copy Sigil embedded Python packages.")
    parser.add_argument("--requirements", required=True, type=pathlib.Path)
    parser.add_argument("--cache-dir", required=True, type=pathlib.Path)
    parser.add_argument("--dest", type=pathlib.Path)
    parser.add_argument("--packages", nargs="+", default=[])
    parser.add_argument("--prepare-only", action="store_true")
    args = parser.parse_args()

    requirements_path = args.requirements.resolve()
    cache_dir = args.cache_dir.resolve()
    cache_dir.mkdir(parents=True, exist_ok=True)

    if args.prepare_only:
        cache_site_packages(cache_dir, requirements_path)
        return 0

    if args.dest is None:
        parser.error("--dest is required unless --prepare-only is used")
    if not args.packages:
        parser.error("--packages is required unless --prepare-only is used")

    return sync_packages(requirements_path, cache_dir, args.dest.resolve(), args.packages)


if __name__ == "__main__":
    sys.exit(main())
