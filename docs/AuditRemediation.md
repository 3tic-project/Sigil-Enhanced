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
| Image hover preview | Pending | Background scaled decode, cancellation, bounded cache |
| Book Browser fast clear | Pending | Batched removal with before/after measurements |

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
