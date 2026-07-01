# Sigil-Enhanced CI Packaging

This document records the packaging assumptions used by
`.github/workflows/package-enhanced.yml`.

## Scope

The workflow builds unsigned CI packages that include the bundled Python
runtime used by Sigil-Enhanced:

- Windows x64: Inno Setup `.exe` installer.
- Windows x86: Inno Setup `.exe` installer, manually enabled only.
- macOS Intel: `.dmg` and `.tar.xz` containing `Sigil.app`.
- macOS ARM: `.dmg` and `.tar.xz` containing `Sigil.app`.

The workflow is intended for release-candidate packaging and manual testing. It
does not sign Windows binaries, sign macOS app bundles, notarize macOS packages,
or publish GitHub Releases.

## Runner Selection

The workflow uses hosted virtual machines rather than containers. Sigil
packaging depends on platform SDKs and Qt deployment tools (`windeployqt`,
`macdeployqt`, Inno Setup, Xcode command line tools), so containers are not a
good fit for the desktop package jobs.

Current runner assumptions:

- `windows-2025-vs2026` for Windows x64 and x86 builds. The checked-in Qt
  archive is built for the VS2026 toolchain, so the workflow locates Visual
  Studio with `vswhere` and then calls `vcvarsall.bat`.
- `macos-15-intel` for Intel macOS packages.
- `macos-latest` for ARM macOS packages, matching the existing upstream Sigil
  ARM workflow in this repository.

Useful references:

- GitHub hosted runner reference:
  https://docs.github.com/en/actions/reference/github-hosted-runners-reference
- GitHub runner images:
  https://github.com/actions/runner-images

## Python Runtime Sources

Windows uses `actions/setup-python` with Python `3.14.2` and the requested
architecture. During CMake configure, `winvirtpy.cmake` creates a cached virtual
environment and installs `src/Resource_Files/python_pkg/winreqs.txt`, including
the PySide6 version matching `QTVER`. The installer target then gathers the
Python runtime into the Inno Setup package with
`windows_python_gather6.py`.

macOS uses the relocatable `Python.framework` archives from
`kevinhendricks/BuildSigilOnMac`. The official python.org macOS framework
installer is stable, but it is not directly suitable for app bundle relocation
without the framework/rpath work documented in
`docs/Building_A_Relocatable_Python_3.14_Framework_on_MacOSX.txt`. The CI
therefore follows the existing Sigil macOS packaging route and bundles that
relocatable framework with `osx_add_python_framework6.py`.

Useful references:

- `actions/setup-python`: https://github.com/actions/setup-python
- Official Windows Python downloads:
  https://www.python.org/downloads/windows/
- macOS relocatable framework assets:
  https://github.com/kevinhendricks/BuildSigilOnMac/releases/tag/for_sigil_1.0.0

## Qt Runtime Sources

The workflow currently pins Qt to `6.10.2`.

Windows x64 uses:

`https://github.com/dougmassay/win-qtwebkit-5.212/releases/download/v5.212-1/Qt6.10.2ci_x64_VS2026.7z`

macOS Intel uses:

`https://github.com/kevinhendricks/BuildSigilOnMac/releases/download/for_sigil_1.0.0/Qt6102.tar.xz`

macOS ARM uses:

`https://github.com/kevinhendricks/BuildSigilOnMac/releases/download/for_sigil_1.0.0/Qt6102_arm64.tar.xz`

Windows x86 is not enabled by default because the current
`dougmassay/win-qtwebkit-5.212` release provides Qt `6.10.2` for Windows x64,
but not Windows x86. The workflow can still run the x86 job when manually
triggered, but `qt_windows_x86_url` must point to a compatible 32-bit Qt archive
that contains `lib/cmake/Qt6/Qt6Config.cmake`.

Useful reference:

- Windows Qt/WebKit assets:
  https://github.com/dougmassay/win-qtwebkit-5.212/releases/tag/v5.212-1

## Cache Strategy

The workflow caches:

- Downloaded/extracted Qt archives per platform.
- Downloaded/extracted macOS `Python.framework` archives per architecture.
- pip download caches.
- Sigil-Enhanced Python runtime package caches via `SIGIL_PYTHON_CACHE_DIR`.

This avoids repeated runtime downloads when rebuilding from a clean CMake build
directory. If a cached runtime becomes stale, update the relevant requirements
file or bump the reset marker used by the existing workflows:

- `.github/workflows/reset-win-caches.txt`
- `.github/workflows/reset-mac-caches.txt`
- `.github/workflows/reset-mac_arm64-caches.txt`

## Triggering

Manual trigger:

1. Open GitHub Actions.
2. Select `Package Sigil-Enhanced`.
3. Run the workflow.
4. Enable `build_windows_x86` only if a valid 32-bit Qt archive URL is supplied.

Tag trigger:

- Pushing tags that match `v*` or `2.*` builds Windows x64 and both macOS
  packages.
- Windows x86 is skipped for tag builds until a maintained 32-bit Qt runtime
  source is available.

## Maintenance Notes

- Keep `QT_VERSION`, `QTVER`, PySide6, and the Qt archive URLs in sync.
- When changing Python package requirements, update
  `requirements-core.txt`/`winreqs.txt`; cache keys already include those files.
- CI packages are unsigned. Release signing and notarization should be added as
  a separate workflow once certificates and secrets are available.
- If GitHub changes runner labels, update this workflow and this document
  together.
