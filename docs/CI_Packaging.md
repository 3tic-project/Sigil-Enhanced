# Sigil-Enhanced CI Packaging

This document records the packaging assumptions used by
`.github/workflows/package-enhanced.yml`.

## Scope

The workflow builds unsigned CI packages that include the bundled Python
runtime used by Sigil-Enhanced:

- Windows x64: Inno Setup `.exe` installer.
- Windows x86: Inno Setup `.exe` installer, manually enabled only.
- macOS Intel: `.dmg` containing `Sigil-Enhanced.app`.
- macOS ARM: `.dmg` containing `Sigil-Enhanced.app`.

Manual runs are intended for release-candidate packaging and testing. A matching
version tag publishes the x64 Windows installer and both macOS packages to the
tag's GitHub Release. The workflow does not sign Windows binaries, sign macOS
app bundles, or notarize macOS packages.

## Runner Selection

The workflow uses hosted virtual machines rather than containers. Sigil
packaging depends on platform SDKs and Qt deployment tools (`windeployqt`,
`macdeployqt`, Inno Setup, Xcode command line tools), so containers are not a
good fit for the desktop package jobs.

Current runner assumptions:

- `windows-2025` for Windows x64 and x86 builds. Windows x64 uses the official
  Qt `win64_msvc2022_64` packages installed by `aqtinstall`; the workflow
  locates Visual Studio with `vswhere` and then calls `vcvarsall.bat`.
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

Windows x64 uses the official Qt online repository through `aqtinstall`:

`aqt install-qt windows desktop 6.10.2 win64_msvc2022_64 -m qt5compat qtwebengine qtwebchannel qtpositioning`

macOS Intel uses:

`https://github.com/kevinhendricks/BuildSigilOnMac/releases/download/for_sigil_1.0.0/Qt6102.tar.xz`

macOS ARM uses:

`https://github.com/kevinhendricks/BuildSigilOnMac/releases/download/for_sigil_1.0.0/Qt6102_arm64.tar.xz`

Windows x86 is not enabled by default because current official Qt 6 packages and
the previously used custom Qt archive do not provide a maintained 32-bit
Windows Qt/WebEngine package for this project. The workflow can still run the
x86 job when manually triggered, but `qt_windows_x86_url` must point to a
compatible 32-bit Qt archive that contains `lib/cmake/Qt6/Qt6Config.cmake`.

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
- The tag, after removing one optional leading `v`, must exactly match the CMake
  version. For example, CMake version `2.8.1E5` accepts `v2.8.1E5` and
  `2.8.1E5`; a mismatched tag fails before package runners start.
- Windows x86 is skipped for tag builds until a maintained 32-bit Qt runtime
  source is available.

Recommended release command:

```sh
git tag -a v2.8.1E5 -m "Sigil-Enhanced 2.8.1E5"
git push enhanced v2.8.1E5
```

## GitHub Release Publishing

The `release` job runs only for tag events and only after Windows x64 and both
macOS matrix builds succeed. It:

1. downloads the three package artifacts from the same workflow run;
2. rejects missing, duplicate, or unexpected package types;
3. creates `SHA256SUMS.txt` over the exact `.exe` and `.dmg` files;
4. creates a GitHub Release with generated notes, or reuses an existing Release
   for the same tag when a failed workflow is rerun;
5. uploads all packages and the checksum file, replacing same-named assets on a
   rerun.

Only this job receives `contents: write`; checkout and package jobs retain the
workflow's default read-only permission. It uses the repository-scoped
`GITHUB_TOKEN`, so no personal access token or additional secret is required.
Manual `workflow_dispatch` builds remain Actions artifacts and never publish or
modify a Release.

The resulting Release packages are currently unsigned and unnotarized. This is
unchanged from the previous CI artifacts and should remain visible in release
notes until signing is implemented.

## Maintenance Notes

- Keep `QT_VERSION`, `QTVER`, PySide6, and the Qt archive URLs in sync.
- When changing Python package requirements, update
  `requirements-core.txt`/`winreqs.txt`; cache keys already include those files.
- The first-party MCP adapter requires the stable `mcp==1.28.1` runtime. macOS
  copies the complete isolated requirement tree so native and transitive MCP
  dependencies are not omitted; Windows gathers the same dependency through
  `winreqs.txt`.
- CI packages are unsigned. Release signing and notarization should be added as
  a separate workflow once certificates and secrets are available.
- If GitHub changes runner labels, update this workflow and this document
  together.
