# Chinese Conversion

This document describes the implemented Simplified/Traditional Chinese conversion workflow in
Sigil Enhanced. The synchronized Chinese guide is [`ChineseConversion.md`](ChineseConversion.md).

## Available functionality

- OpenCC 1.3.1 is statically built with 12 stable conversion profiles.
- Scopes: current plain-text selection, current XHTML/SVG file, selected XHTML/SVG resources, and
  all XHTML/SVG resources.
- Preferences, an execution dialog, and a preview table with per-change opt-out are available.
- Current selection/file edits form one editor undo step.
- Batch conversion performs read-only analysis, requires preview, creates one Checkpoint, rechecks
  every source snapshot, and writes each changed resource once.
- OpenCC data and its license are bundled for macOS, Windows, and Linux. Runtime behavior does not
  depend on a system OpenCC installation, `PATH`, or the working directory.

NCX, OPF metadata, CSS generated content, custom/protected dictionaries, reports, automation APIs,
language metadata changes, and font coverage checks are not implemented yet.

## Workflow

Open `Enhancement > Chinese Conversion...`, choose a mode and scope, adjust protected content and
attribute options, then preview or convert. The preview lists resource, XML location, before, and
after values. Uncheck changes that should not be applied.

A selection containing `<` or `&` is never replaced as plain source text. Sigil offers to switch to
structure-aware current-file conversion. Batch scopes always show a preview and require a successful
Checkpoint before writing resources.
If Checkpoint creation fails, the pre-call OPF content and book modified state are restored.

Defaults are stored on the `Chinese Conversion` Preferences page using stable profile and scope keys.

## Safety boundary

`ChineseTextConversionPlan` parses XHTML/SVG with Gumbo but does not serialize the document. It maps
allowed text and attributes back to the original UTF-8 buffer and applies non-overlapping byte patches
in reverse order. Unchanged declarations, whitespace, attribute order, and quote style remain intact.
Chinese conversion explicitly disables the editor's optional whole-document NFC normalization.

XHTML converts body text plus enabled `alt`, `title`, `aria-label`, and `aria-description` attributes.
It protects scripts, styles, code-like elements, Japanese-language subtrees, structural attributes,
URLs, IDs, classes, and the document head. SVG converts text containers and accessibility text only;
paths, metadata, IDs, references, transforms, and geometry remain untouched.

## Profiles

Stable keys are `s2t`, `t2s`, `s2tw`, `tw2s`, `s2hk`, `hk2s`, `s2twp`, `tw2sp`, `t2tw`, `tw2t`,
`t2hk`, and `hk2t`. Only `s2twp` and `tw2sp` explicitly include regional vocabulary substitution.

## Core API

```cpp
const auto profile = ChineseConversionProfile::ForMode(ChineseConversionMode::S2TWP);
OpenCCConverter converter(profile, ChineseConversionData::FindDataDirectory());
const auto plan = ChineseTextConversionPlan::Build(
    source, ChineseDocumentKind::Xhtml, options, converter);
QString error;
const QString output = plan.Apply(enabledChangeIndexes, &error);
```

Do not commit output when initialization, planning, or apply reports an error. Settings decoding falls
back to `s2t` and `current_file` for unknown values.

## Build and test

```sh
cmake -S . -B cmake-build-debug -DBUILD_TESTING=ON
cmake --build cmake-build-debug --target Sigil \
  chinese_conversion_core_test chinese_conversion_markup_test -j4
ctest --test-dir cmake-build-debug --output-on-failure
```

The core test covers all profiles, data discovery, representative conversions, missing data, settings
round trips, and invalid-setting fallbacks. The markup test covers XHTML/SVG allowlists, entities,
Japanese inheritance, protected fields, negative structural assertions, and partial preview apply.
The current macOS Debug build and all 20 CTest cases pass.

Runtime data is installed at `Sigil.app/Contents/opencc` on macOS, beside the application on Windows,
and under `<share>/sigil/opencc` on installed Linux systems. Vendoring details are recorded in
[`3rdparty/opencc/SIGIL_VENDORING.md`](../3rdparty/opencc/SIGIL_VENDORING.md).

## Remaining work

Large-book analysis still runs on the GUI thread. Later phases must add cancellable background
analysis, explicit NCX/OPF allowlists, protected/custom dictionaries, durable reports, automation and
plugin APIs, language metadata updates, and font coverage checks. Until then, never pass complete XML,
OPF, NCX, or CSS source directly to OpenCC.
