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
| Safe archive paths and budgets | Pending | ZIP Slip and ZIP bomb cases fail safely; normal EPUB/plugin archives pass |
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
