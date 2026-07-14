# Audit Remediation Status

Baseline: `099adbd1d95902c2d858e8c5b5459024654ed17e`
Started: 2026-07-14

This document tracks implementation and verification of the first four findings in
`todo/audit/00-审计总览与执行摘要.md` and the corresponding immediate and performance
work in `todo/audit/06-整改路线图与验收标准.md`.

## Progress

| Work item | Status | Acceptance summary |
| --- | --- | --- |
| Singleton self-deletion | Complete | Eight destructors only clear their own registered instance pointer; Debug build passes |
| FindReplacePlus double initialization | Complete | One initialization; all option lists are idempotent and have asserted counts |
| CSSInfo leak | Complete | Inline style formatting uses scoped parser ownership; Debug build passes |
| Safe archive paths and budgets | Complete | Shared extractor enforces path/resource limits; negative corpus and real EPUB/plugin fixtures pass |
| Image hover preview | Complete | Background scaled decode, stale-request cancellation, 32 MiB LRU; service tests pass |
| Book Browser fast clear | Complete | Folder/root removal and category insertion are batched; 5000-node model rebuild is 2.405 ms |

## Singleton Destruction

### Impact

The audit estimated seven affected classes. Source review found eight:
`SearchEditorModelPlus`, `PluginDB`, `IndexEditorModel`, `ClipEditorModel`,
`IndexEntries`, `SpellCheck`, `SearchEditorModel`, and `EmbeddedPython`.
Their destructors deleted `m_instance` while it could point to the object already
being destroyed, recursively entering the destructor and risking a double free or
stack exhaustion.

### Change Boundary

Each destructor now clears the static pointer only when `m_instance == this` and
does not attempt to own or delete itself. Existing cleanup order for dictionaries,
plugins, file watchers, and the embedded Python interpreter is unchanged. Singleton
creation and process shutdown timing are outside this change.

### Verification

- Static regression: no `delete m_instance` remains under `src/`.
- Build regression: `cmake --build cmake-build-debug -j2` passes.
- Failure path: destroying an object that is not the registered instance does not
  overwrite the pointer to the registered instance.
- No UI strings, persistence formats, transaction boundaries, or undo behavior change.

## FindReplacePlus Initialization

### Impact

The constructor called `ExtendUI()` both before and after replacing the combo-box
line edits. Most option lists were cleared by that function, but the Marked Text
indicator was not, so it accumulated two identical entries.

### Change Boundary

The premature call was removed. `ExtendUI()` now also clears the Marked Text
indicator, making repeated calls idempotent. The existing order in which custom line
edits, completers, options, signal connections, and settings are initialized is kept.

### Verification

- Debug assertions verify 3 search modes, 6 search scopes, 2 directions, and exactly
  1 Marked Text entry after every call to `ExtendUI()`.
- Build regression: `cmake --build cmake-build-debug -j2` passes with assertions enabled.
- Repeated initialization no longer changes any option count.
- Existing translated strings are reused; no translation catalogs or settings keys change.

## Inline CSS Parser Lifetime

### Impact

`CleanSource::PrettifyXhtml()` allocated one `CSSInfo` for every non-empty inline
`style` block and never deleted it. Reformatting books with many embedded styles
therefore retained parser tokens and selector objects for the rest of the process.

### Change Boundary

The parser is now a block-scoped stack object. Its destructor runs immediately after
the formatted CSS is obtained. Formatting, indentation, well-formedness checks,
progress reporting, and resource transaction behavior are unchanged. Other
`CSSInfo` heap allocations were reviewed and retain explicit owning cleanup.

### Verification

- Static regression: the inline style path contains no `new CSSInfo`.
- Ownership regression: all remaining `new CSSInfo` calls are held by
  `HTMLStyleInfo` or `BookReports` and have matching cleanup.
- Build regression: `cmake --build cmake-build-debug -j2` passes.
- The parser output call and `cssfold` option are unchanged; no UI or translation changes.

## Safe Archive Extraction

### Impact And Trust Boundary

EPUB files and plugin ZIPs are untrusted inputs. Their former extraction loops used
path string rewriting and had no file-count, expanded-size, or compression-ratio
budget. A crafted archive could consume disk and UI time, and malformed path forms
were handled differently between the two entry points. Plugins remain trusted local
code after installation; this change secures installation, not plugin execution.

### Change Boundary

`SafeArchiveExtractor` is now the only writing extraction implementation used by
EPUB import, plugin installation, and `Utility::UnZip`. It applies these defaults:

| Boundary | Default |
| --- | ---: |
| Entries | 100,000 |
| Single expanded file | 2 GiB |
| Total expanded data | min(8 GiB, archive size x 200) |
| Per-file compression ratio | 200:1 |
| Path depth | 64 segments |
| Path length | 1,024 characters |

Paths are normalized to NFC and must remain lexically below the destination.
Absolute, drive, UNC/backslash, empty, dot, parent, colon, trailing-dot/space, and
Windows device-name segments are rejected. Duplicate normalized paths, platform
case collisions, symbolic-link entries, and existing parent symlinks are rejected.
UTF-8 names are decoded strictly and legacy names use a local CP437 table.

Declared sizes are checked before each write and actual streamed bytes are checked
again while writing. Files use `QSaveFile`; any error, CRC mismatch, budget breach,
or cancellation removes content created by that extraction. Existing destination
files are never overwritten. Plugin ZIPs extract into a same-filesystem temporary
directory, validate their single top-level folder and `plugin.xml`, then commit by
rename. Forced updates keep a backup and restore it if loading the replacement fails.

### Verification

- `safe_archive_extractor_test` covers normal EPUB-like and plugin layouts; parent,
  absolute, drive, backslash, empty-segment, device-name, and symlink-parent paths;
  Unicode normalization; duplicate paths; file-count, single-file, total-size,
  compression-ratio, depth, and length budgets; cancellation; and partial cleanup.
- Repository fixtures `docs/testplugin_v020.zip` and
  `docs/Sigil_Plugin_Framework_rev15.epub` extract successfully.
- `ctest --test-dir cmake-build-debug --output-on-failure` passes.
- A separate AddressSanitizer build passes the same test corpus; leak detection is
  disabled because the macOS ASan runtime reports it as unsupported.
- `cmake --build cmake-build-debug -j2` passes, including Simplified and Traditional
  Chinese catalogs for every new user-visible archive error.

The cancellation callback is implemented and tested, but current synchronous EPUB
and plugin callers do not yet expose a UI cancel control. Archive processing has no
EPUB undo transaction; a failed import cleans its files, while plugin replacement is
handled by the staging/backup transaction described above.

## Image Hover Preview

### Impact

Book Browser hover previously constructed a full `QPixmap` from each bitmap on the
GUI thread and then scaled it. Large or adversarial images could therefore block
event processing and create a source-sized memory peak; repeated hover decoded the
same file again because there was no cache.

### Change Boundary

`ImagePreviewService` performs all file inspection, bitmap decoding, and SVG parsing/
rendering through `QtConcurrent`. Bitmap previews use `QImageReader::setScaledSize`
before `read()`, and SVGs render directly to a bounded `QImage`. Both outputs have a
maximum side of 300 pixels. Source files over 128 MiB are not previewed.

The GUI thread only submits a request, validates the returned persistent model index
and resource path, converts the already-scaled image into the popup pixmap, and
positions the popup. Moving to another item, scrolling, dragging, or hiding the view
invalidates active request IDs and sets cooperative cancellation tokens, so stale
results are discarded even if a third-party decoder cannot stop mid-call.

Successful results use a modification-aware key (absolute path, size, mtime, format)
in a 32 MiB `QCache` LRU. Cached delivery is still deferred to the event loop, which
keeps request ordering consistent. Invalid or over-budget input hides the preview;
it does not modify the resource, book, undo history, or persisted settings.

### Verification

- `image_preview_service_test` verifies scaled bitmap decode and original dimensions,
  SVG rendering, callback delivery on the service/GUI thread, stale-result
  cancellation, cache hits, and eviction below a configured byte ceiling.
- A 20,000 x 20,000 SVG with 8,000 shapes is used for the UI-blocking regression.
  On the local macOS Debug build, equivalent synchronous decode/render took
  31.892 ms while the asynchronous request returned in 0.068 ms. The request path is
  asserted below 16 ms in the test; background completion time is intentionally not
  counted as GUI blocking.
- The production cache limit is 32 MiB; a 700 KiB test cache evicts older 300-pixel
  previews and never reports cost above its limit.
- The image preview test also passes in the separate AddressSanitizer build
  (`detect_leaks=0` because the macOS runtime does not support leak detection).
- `cmake --build cmake-build-debug -j2` and the complete CTest suite pass.
- No user-visible strings or translation catalogs change in this batch.

## Book Browser Model Rebuild

### Impact

`OPFModel::ClearModel()` removed row zero repeatedly in each of seven resource
folders. A 5,000-resource refresh consequently emitted 5,002 removal notifications,
then `InitializeModel()` emitted another notification for every appended resource.
Even where Qt's item removal itself remained fast, the view and connected model
observers had to process thousands of intermediate structural changes.

### Change Boundary

Each resource folder now removes its full row range with one `removeRows()` call.
Non-folder root items such as OPF and NCX are removed as reverse contiguous ranges,
which preserves the seven long-lived folder objects regardless of root ordering.

During initialization, new items are accumulated into Text, Styles, Images, Fonts,
Misc, Audio, Video, and root lists, then appended with one `appendRows()` call per
non-empty list. The view already has automatic sorting disabled. The existing final
filename and reading-order sorts remain the only sorts performed by `Refresh()`.
Signals are not globally blocked: the view still receives valid batched remove and
insert notifications, and `m_RefreshInProgress` continues to prevent removal events
from changing the EPUB reading order during a rebuild.

### Verification

`opf_model_clear_benchmark` builds equivalent seven-folder models and compares the
former row-at-a-time clear/repopulate pattern with the batched pattern in the same
macOS Debug process:

| Resources | Former | Batched | Remove signals | Insert signals |
| ---: | ---: | ---: | ---: | ---: |
| 100 | 0.081 ms | 0.057 ms | 102 -> 8 | 102 -> 8 |
| 1,000 | 0.707 ms | 0.474 ms | 1,002 -> 8 | 1,002 -> 8 |
| 5,000 | 3.970 ms | 2.405 ms | 5,002 -> 8 | 5,002 -> 8 |

- The 5,000-node batched model operation is asserted below the 500 ms target.
- Ten consecutive benchmark runs pass, as does the AddressSanitizer build
  (`detect_leaks=0` on the macOS runtime).
- The full Debug build and complete CTest suite pass.
- This is a model-structure benchmark, not an end-to-end `Refresh()` measurement;
  OPF parsing, semantic lookup, icon creation, and final sorting remain future
  optimization targets.
- No resource data, OPF transaction, undo behavior, UI text, or translations change.

## Completion Summary

The requested audit slice was completed on 2026-07-14 in six independently tested
implementation commits:

| Commit | Work item |
| --- | --- |
| `9c95ae1cc` | Remove recursive self-deletion from eight singleton destructors |
| `152917798` | Make FindReplacePlus initialization idempotent |
| `f4760e56b` | Give inline CSS parsers scoped lifetime |
| `07f33fd68` | Add shared safe archive extraction and transactional plugin install |
| `1bf3868ee` | Move image preview decoding off the GUI thread |
| `1ba27c4a4` | Batch Book Browser model removal and insertion |

### Final Verification

- A clean macOS Debug build completed all 466 build steps and linked Sigil.
- All three CTest targets pass: safe archive extraction, image preview service, and
  OPF model clear/rebuild benchmark.
- All three targets also pass in an AddressSanitizer build. The macOS ASan runtime
  does not support leak detection, so those runs use `detect_leaks=0`.
- Static regression checks confirm no singleton `delete m_instance`, duplicate
  FindReplacePlus constructor initialization, leaking inline `new CSSInfo`, archive
  writer outside `SafeArchiveExtractor`, GUI-thread file `QPixmap` preview decode, or
  `removeRow(0)` loop in `OPFModel::ClearModel` remains.
- Simplified and Traditional Chinese archive error catalogs compile, and both TS
  files pass XML validation.

### Remaining Boundaries

- EPUB/plugin extraction supports cancellation internally, but the current
  synchronous UI entry points do not expose a cancel control.
- Image parsing is off the GUI thread and memory-bounded at the output/cache layers,
  but third-party decoders are not isolated in a helper process.
- The Book Browser benchmark covers model structure only, not full OPF parsing,
  semantic lookup, icon creation, or sorting.
- The clean build reports pre-existing warnings in `SearchEditorTreeView.cpp`,
  `CodeCompleterParser.cpp`, and `CompletionWords.cpp`; none are in files changed by
  this remediation series.
