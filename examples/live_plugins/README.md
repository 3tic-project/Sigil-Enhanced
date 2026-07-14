# Sigil Live Python Plugin Examples

Each child directory is an installable plugin. Copy one directory into Sigil's
user plugin directory, or zip its contents and install it with the Plugin
Manager. These examples use only the Python standard library and the bundled
`sigil_live` SDK.

| Plugin | Purpose |
| --- | --- |
| `LiveApiShowcase` | Book/package reads, editor navigation and edits, UI, progress, and subscriptions. |
| `LiveTransactionLab` | Text, binary, structure, package, and unmanaged archive transactions. |
| `LiveValidationExample` | Publish structured validation results. |
| `LiveInputExample` | Select and stream an EPUB that will replace the current Book. |
| `LiveOutputExample` | Rebuild a current in-memory EPUB to a user-selected output path. |
| `LiveBookWatcher` | Long-running `book-session` event consumer. |

`LiveApiShowcase` and `LiveTransactionLab` ask before changing the Book. The
input example uses Sigil's normal unsaved-change confirmation before replacing
the current Book. The output example refuses to overwrite the file currently
open in Sigil.

See `docs/LivePythonPluginAPIReference.md` for the complete API contract.
