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
the PySide6 version matching `QTVER`. The installer target gathers the base
Python runtime with `windows_python_gather6.py`, then synchronizes the complete
`requirements-windows.txt` tree into `Lib/site-packages`. The generated embedded
`site.py` processes packaged `.pth` files so path-based packages such as
`pywin32` can load their Python modules and DLL bootstrap code.

macOS uses the relocatable `Python.framework` archives from
`kevinhendricks/BuildSigilOnMac`. The official python.org macOS framework
installer is stable, but it is not directly suitable for app bundle relocation
without the framework/rpath work documented in
`docs/Building_A_Relocatable_Python_3.14_Framework_on_MacOSX.txt`. The CI
therefore follows the existing Sigil macOS packaging route and bundles that
relocatable framework with `osx_add_python_framework6.py`.
The build then synchronizes the complete locked dependency tree into
`Sigil-Enhanced.app/Contents/python3lib`.

Useful references:

- `actions/setup-python`: https://github.com/actions/setup-python
- Official Windows Python downloads:
  https://www.python.org/downloads/windows/
- macOS relocatable framework assets:
  https://github.com/kevinhendricks/BuildSigilOnMac/releases/tag/for_sigil_1.0.0

## Python Dependency Lock And Verification

`src/Resource_Files/python_pkg/requirements-core.txt` is the canonical lock for
the 42 platform-neutral distributions used by Sigil's bundled Python features.
It includes direct requirements and every platform-neutral transitive
dependency, all with exact versions. `requirements-windows.txt` adds the locked
Windows-only transitive dependencies `colorama==0.4.6` and `pywin32==312`.
`winreqs.txt` contains that 44-distribution Windows runtime lock plus
`PySide6==${QTVER}`. The CI helper lock at
`.github/workflows/requirements.txt` contains the core set plus the Qt version
selected by that workflow.

Every packaged runtime is checked by `ci_scripts/verify_bundled_python.py` after
copying:

- installed distribution metadata must contain the exact locked versions;
- imports run with isolated `sys.path` values rooted only in the packaged
  directories, including normal `.pth` processing; a `.pth` path that escapes
  those roots is rejected, preventing the build machine's site-packages from
  masking an incomplete package;
- Windows and packaged macOS builds additionally verify PySide6;
- a missing distribution, mismatched version, or failed import stops the build.

Windows, both macOS architectures, and the AppImage build use the same complete
package synchronization and verification path. Before changing the lock, audit
binary wheel availability for the supported Python version and each target
architecture. A package that silently falls back to a source build can introduce
an undeclared compiler or Rust dependency; `cryptography==48.0.0` is retained
because it provides compatible wheels for the current target matrix.

## Qt Runtime Sources

The workflow currently pins Qt to `6.10.2`.

Windows x64 uses the official Qt online repository through `aqtinstall`:

`aqt install-qt windows desktop 6.10.2 win64_msvc2022_64 -m qt5compat qtwebengine qtwebchannel qtpositioning qtimageformats`

Qt 6.10 `WebEngineCore` requires `Qt6Positioning` at CMake configure time.
ImageTab / Adjust Image decode WebP through Qt's `qwebp.dll` plugin, which
lives in the `qtimageformats` module rather than qtbase. Without that module
the Windows package only ships `qgif`/`qico`/`qjpeg`/`qsvg` and reports
`The Qt WebP plugin is not available.` The Windows job therefore treats a
cache as incomplete unless it has `Qt6Positioning`, `Qt6WebEngineCore`,
`Qt6WebEngineWidgets`, `Qt6WebChannel`, `Qt6Core5Compat`, **and**
`plugins/imageformats/qwebp.dll` before skipping `aqtinstall`. CMake
configuration of the Windows installer fails if the plugin is still missing.
Bump `.github/workflows/reset-win-caches.txt` after changing the required
Qt module set.

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
  version. For example, CMake version `2.8.1E8` accepts `v2.8.1E8` and
  `2.8.1E8`; a mismatched tag fails before package runners start.
- Windows x86 is skipped for tag builds until a maintained 32-bit Qt runtime
  source is available.

Recommended release command:

```sh
git tag -a v2.8.1E8 -m "Sigil-Enhanced 2.8.1E8"
git push enhanced v2.8.1E8
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
- When changing Python packages, keep the core entries in
  `requirements-core.txt`, `requirements-windows.txt`, `winreqs.txt`, and
  `.github/workflows/requirements.txt` identical. Add Windows-only dependencies
  to both Windows files. The automated package-sync test enforces these
  invariants, and cache keys include the platform requirements files.
- The first-party MCP adapter requires the locked `mcp==1.28.1` runtime. Do not
  remove apparently indirect packages from the lock without repeating the
  isolated-import and cross-platform wheel audits.
- CI packages are unsigned. Release signing and notarization should be added as
  a separate workflow once certificates and secrets are available.
- If GitHub changes runner labels, update this workflow and this document
  together.
