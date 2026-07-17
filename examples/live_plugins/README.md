# Sigil Live Python Plugin Examples

Each child directory contains an installable plugin. Sigil's Plugin Manager
accepts ZIP archives, not source directories. Build an installer-compatible
archive with:

```console
python3 examples/live_plugins/package_plugin.py examples/live_plugins/SigilMcpServer
```

The generated `SigilMcpServer.zip` has the required `SigilMcpServer/` top-level
directory and omits operating-system metadata, Python caches, and nested ZIPs.
Most examples use only the Python standard library and bundled
`sigil_live` SDK. `SigilMcpServer` additionally uses the bundled official MCP
Python SDK.

| Plugin | Purpose |
| --- | --- |
| **`LiveApiCoverage`** | **Full v2 SDK coverage harness (edit/command).** See `LiveApiCoverage/DESIGN.md`. |
| **`LiveApiCoverageInput`** | Coverage companion for `InputApi` / `InputWriter`. |
| **`LiveApiCoverageOutput`** | Coverage companion for `OutputApi`. |
| **`LiveApiCoverageValidation`** | Coverage companion for `ValidationApi`. |
| **`LiveApiCoverageSession`** | Coverage companion for `EventsApi.next_event` (`book-session`). |
| **`SigilMcpServer`** | Local MCP tools/resources/prompts for LLM access to the current in-memory Book. |
| `LiveApiShowcase` | Book/package reads, editor navigation and edits, UI, progress, and subscriptions. |
| `LiveTransactionLab` | Text, binary, structure, package, and unmanaged archive transactions. |
| `LiveValidationExample` | Publish structured validation results. |
| `LiveInputExample` | Select and stream an EPUB that will replace the current Book. |
| `LiveOutputExample` | Rebuild a current in-memory EPUB to a user-selected output path. |
| `LiveBookWatcher` | Long-running `book-session` event consumer. |

## Full API coverage suite

Because the host gates APIs by plugin `type` and `lifetime`, complete SDK
coverage is split across five installable plugins. Install all of them and run
in this order for a full manual pass:

1. **Live API Coverage** (edit) — Book, Editor, UI, Events.`poll`, Transaction
2. **Live API Coverage Input** — choose EPUB + stream upload
3. **Live API Coverage Output** — export / save_source
4. **Live API Coverage Validation** — `publish_results`
5. **Live API Coverage Session** — leave running; edit the book to emit events; Cancel to stop

`api-coverage.json` maps every public `sigil_live` method to the suite file that
calls it; `tests/live_plugin_examples_test.py` enforces that mapping.

Design details: `LiveApiCoverage/DESIGN.md`.

## Notes

`LiveApiShowcase`, `LiveTransactionLab`, and `LiveApiCoverage` ask before
changing the Book. The input example uses Sigil's normal unsaved-change
confirmation before replacing the current Book. The output example refuses to
overwrite the file currently open in Sigil.

See `docs/LivePythonPluginAPIReference.md` for the complete API contract.
See `docs/MCPUserGuide_zh-CN.md` and `docs/MCPToolsReference_zh-CN.md` for MCP
installation, connection, workflows, and the complete tool contract.
