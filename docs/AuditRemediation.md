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
| FindReplacePlus double initialization | Pending | Marked Text has exactly one item |
| CSSInfo leak | Pending | Repeated formatting leaves no orphaned allocation |
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
