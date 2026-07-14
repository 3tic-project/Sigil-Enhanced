# Live Python Plugin API v2

This document describes the live Python plugin implementation in this source
tree. It is the user and API reference for implemented behavior. The broader
design roadmap is not an implementation contract; methods that are not listed
here are not available yet.

## Status and compatibility

Sigil currently contains two independent plugin execution paths:

| Runtime | API | Behavior |
| --- | --- | --- |
| Legacy | v1 containers | Modal, disk-backed snapshot and result import. Existing plugins continue to use this path by default. |
| Live | v2 `sigil_live` SDK | Modeless, local-socket RPC against the in-memory Book and current editor. |

The plugin manager stores a `Legacy (v1)` or `Live (v2)` choice per plugin.
Native live execution uses an explicit `<api version="2" interface="live"/>`
declaration. Selecting Live for an undeclared v1 `edit` plugin invokes the
RPC-backed `CompatBookContainer`, preserving the legacy entry point and public
API. Success commits its implicit transaction; a nonzero return, exception,
cancel, crash, or rejected commit rolls it back. Legacy validation plugins use
a read-only compatibility container and publish structured results after
success. Legacy output plugins use the read-only compatibility wrapper. Legacy
input plugins upload their resulting EPUB through a bounded, integrity-checked
stream; after success, Sigil preserves the existing unsaved-change confirmation
before replacing the current book.

The complete v1 behavior and method inventory are documented in
`LegacyPythonPluginSystem.md`. The legacy launcher and `BookContainer` remain
unchanged and are the compatibility reference.

## Plugin layout

A native v2 command plugin uses the existing plugin directory layout:

```text
<preferences>/plugins/LiveFormatter/
|-- plugin.xml
`-- plugin.py
```

Example `plugin.xml`:

```xml
<?xml version="1.0" encoding="UTF-8"?>
<plugin>
  <name>LiveFormatter</name>
  <author>Example</author>
  <description>Replaces the current editor selection.</description>
  <type>edit</type>
  <engine>python3</engine>
  <version>2.0.0</version>
  <api version="2" interface="live" />
  <lifetime>command</lifetime>
  <permissions>
    <permission>book.read</permission>
    <permission>book.write.text</permission>
    <permission>editor.read</permission>
    <permission>editor.write</permission>
  </permissions>
</plugin>
```

If `permissions` is omitted, edit plugins receive the current compatibility
defaults: `book.read`, `book.write.text`, `book.write.binary`,
`book.structure`, `editor.read`, and `editor.write`. Declaring permissions is
recommended because the RPC dispatcher enforces the declared list.

Only `lifetime=command` is implemented. `plugin.py` must export:

```python
def run(plugin):
    return 0
```

`None` and `0` mean success. Other return values and uncaught exceptions fail
the session. Standard output and standard error are displayed in the modeless
session console.

## Current editor example

```python
def run(plugin):
    state = plugin.editor.get_state()
    if not state.active or state.selection.start == state.selection.end:
        return 0

    replacement = state.selection.text.upper()
    plugin.editor.replace_selection(
        replacement,
        expected_revision=state.revision,
        resource_id=state.resource_id,
        label="Uppercase selection",
    )
    return 0
```

This reads the current `QTextDocument`, including edits that have not been
saved to disk. The replacement is immediately visible and is one Qt undo
step. The host does not call `SaveTabData()` or `SaveAllResourcesToDisk()` to
start a live session.

## Cross-file transaction example

```python
def run(plugin):
    with plugin.book.transaction("Normalize trailing whitespace") as tx:
        for resource in plugin.book.text_resources():
            current = tx.read_text(resource)
            updated = "\n".join(line.rstrip() for line in current["text"].split("\n"))
            if updated != current["text"]:
                tx.replace_text(
                    resource,
                    updated,
                    expected_revision=current["revision"],
                )

        preview = tx.preview()
        if preview["summary"]["modified"]:
            tx.commit()
    return 0
```

Text transactions have staged visibility. Reads observe previous writes in the
same transaction, while the live Book remains unchanged until commit. Leaving
the context without calling `commit()` rolls the transaction back. A crash or
session termination also discards it.

At commit the host rechecks every staged resource revision. An `auto`
transaction creates one Checkpoint for two or more changed resources and for
NCX changes. Each changed text resource receives one final `SetText()` call,
regardless of how many staged patches were composed. OPF writes are currently
rejected until the structure transaction API can validate manifest, spine, and
metadata invariants.

## Python SDK

The bundled `sigil_live` package uses only the Python standard library. The
launcher supplies the private socket name and one-time token; plugins should
not construct the transport directly.

### `Plugin`

| Member | Result |
| --- | --- |
| `session_info` | Negotiated session dictionary. |
| `book` | `BookApi` instance. |
| `editor` | `EditorApi` instance. |
| `input` | `InputApi` instance for input plugins. |
| `ping()` | `True` when the host responds. |
| `finish(status, message)` | Normally called by the launcher. |

### `BookApi`

| Method | Result |
| --- | --- |
| `get_info()` | EPUB version, modified state, file path, and book revision. |
| `get_metadata()` | Ordered metadata entries, raw metadata XML, and package attributes. |
| `get_manifest()` | Ordered manifest entries with href, resolved Book path, properties, and resource ID. |
| `get_spine()` | Ordered spine entries plus spine element attributes. |
| `get_guide()` | EPUB 2 guide entries with resolved paths and fragments. |
| `get_bindings()` | EPUB 3 media type handler bindings. |
| `get_selection()` | Typed resources selected in the Book Browser. |
| `get_compatibility_snapshot()` | Immutable OPF, resource index, selection, UI, spellcheck, automation, and font-mangling startup state for the v1 adapter. |
| `get_revision()` | Current session-local monotonic book revision. |
| `archive_files(page_size=200)` | Iterate every regular expanded-EPUB file, including files outside the Resource model. |
| `read_archive_file(book_path)` | Read up to 5 MiB and return decoded `data`, SHA-256, and protection state. |
| `resources(types=None, page_size=200)` | Iterator of typed `Resource` values. |
| `text_resources()` | Iterator limited to current text resource types. |
| `resolve_path(book_path)` | Resolve a current book path to `Resource`. |
| `get_resource(resource_id)` | Fetch current resource metadata. |
| `read_text(resource)` | Dictionary containing `text` and `revision`. |
| `read_many(resources)` | Up to 100 current text resources in one round trip. |
| `read_binary(resource)` | Read a binary resource up to 5 MiB and return decoded `data`. |
| `open_binary(resource)` | Open a context-managed, chunked snapshot reader for a binary resource of any size. |
| `transaction(label, checkpoint="auto")` | Begin a staged `Transaction`. |

`Resource` contains `id`, `book_path`, `media_type`, `resource_type`,
`revision`, and `loaded`. Its ID is stable only while the current Book is open.

`get_compatibility_snapshot()` is a startup snapshot, not a general
synchronization API. It is protected by `book.read`; writes still go through
revision-checked transactions. Application and preferences paths are included
because the v1 container exposes them for Hunspell, Gumbo, and per-plugin
preferences compatibility.

`open_binary()` returns a `BinaryReader` with `size`, `sha256`, `revision`, and
`chunk_size` metadata. Iterate `reader.chunks()` or call `reader.read()`, and
use it as a context manager so the session-owned temporary snapshot is removed
immediately. Individual chunks are limited to 2 MiB; all remaining snapshots
are removed when the plugin session ends.

### `InputApi`

| Method | Behavior |
| --- | --- |
| `begin_epub(filename, size=None)` | Create an `InputWriter` for a basename ending in `.epub`. |
| `submit_epub(data, filename="input.epub")` | Upload a bytes-like EPUB in negotiated chunks and submit it. |
| `submit_epub_file(path)` | Stream an EPUB file without reading the whole file into Python memory. |

`InputWriter.write(data)` updates a client-side SHA-256 while sending chunks;
`finish()` asks the host to verify that hash, the optional declared length, the
2 GiB limit, and the ZIP signature. Only an input plugin with `input.submit`
may use these methods. Submission does not replace the Book unless the plugin
subsequently finishes successfully.

### `EditorApi`

| Method | Behavior |
| --- | --- |
| `get_state()` | Return typed active resource, cursor, selection, and revision state. |
| `get_selection()` | Return a typed `Selection`. |
| `get_open_tabs()` | Return resources loaded in editor tabs. |
| `apply_edits(edits, expected_revision, resource_id, label)` | Apply non-overlapping text edits to the active tab. |
| `replace_selection(text, expected_revision, resource_id, label)` | Replace the active selection. |
| `insert_text(text, expected_revision, resource_id, label)` | Insert at the active cursor. |
| `set_cursor(position, resource_id=None)` | Move the current editor cursor. |
| `set_selection(start, end, resource_id=None)` | Select a current editor range. |
| `open_resource(resource, position=None)` | Open/activate a resource and optionally reveal a UTF-16 position. |
| `reveal_range(resource, start, end)` | Open a resource and select a validated UTF-16 range. |

An edit can be a dictionary with `start`, `end`, and `text`, or a
`(start, end, text)` tuple. A request contains between 1 and 1000 edits. Ranges
must be in bounds, non-overlapping, and cannot split a UTF-16 surrogate pair.
The host sorts them in descending position order before applying them in one
edit block.

### `UiApi`

| Method | Behavior |
| --- | --- |
| `show_status(message, duration_ms=5000)` | Display a status-bar message for 0-60 seconds. |
| `show_message(message, title=None, level="info")` | Display an information, warning, or error dialog. |
| `confirm(message, title=None)` | Ask a yes/no question; No is the default. |

Navigation requires `ui.navigate`; messages and confirmation require
`ui.message`. Dialog text and status duration are bounded by the wire contract.

### `EventsApi`

```python
plugin.events.subscribe("editor.activeChanged", "book.resourceChanged")
event = plugin.events.next_event()
print(event["name"], event["params"])
```

Supported events are `editor.activeChanged`, `book.resourceChanged`,
`book.resourceAdded`, and `book.resourceRemoved`. Book events require
`events.book`; editor events require `events.editor`. Subscriptions declared in
`plugin.xml` are installed at session startup when their permissions are
granted. Every event contains `book_revision` and `origin_session_id`; the
origin is this session for changes synchronously caused by its request and
`None` for external/user changes.

`poll()` consumes an already queued event without blocking. `next_event()`
waits using the transport timeout. The SDK queues notifications received while
waiting for ordinary RPC results. Calls and event consumption on a single
connection must remain single-threaded.

### `ValidationApi`

`plugin.validation.publish_results(results)` accepts dictionaries with `type`
(`info`, `warning`, or `error`), `message`, optional canonical `book_path`, and
optional `line`/`character` positions. Unknown positions are `-1`. Only
validation plugins with `validation.publish` may call it. A call atomically
replaces the Validation Results view and accepts at most 10,000 entries.

### `Transaction`

| Method | Behavior |
| --- | --- |
| `read_text(resource)` | Read staged content when present, otherwise live content. |
| `replace_text(resource, text, expected_revision=None)` | Stage a whole-text replacement. |
| `apply_edits(resource, edits, expected_revision=None)` | Compose patches against staged content. |
| `read_binary(resource)` | Read staged binary data when present. |
| `write_binary(resource, data, expected_revision=None)` | Stage a bytes-like replacement up to 5 MiB. |
| `replace_package(text, expected_revision)` | Stage a complete OPF replacement after XML, version, manifest, and spine validation. |
| `replace_archive_file(book_path, data, expected_sha256)` | Stage replacement of an existing untracked archive file. |
| `remove_archive_file(book_path, expected_sha256)` | Stage removal of an existing untracked archive file. |
| `add_resource(book_path, data, media_type, ...)` | Stage a manifested or unmanifested resource addition. |
| `remove_resource(resource, expected_revision=None)` | Stage removal while protecting OPF, nav, and the last XHTML. |
| `move_resource(resource, book_path, expected_revision=None)` | Stage a canonical Book-path relocation. |
| `rename_resource(resource, filename, expected_revision=None)` | Stage a filename change in the current folder. |
| `validate()` | Return conflicts without changing the Book. |
| `preview()` | Return counts and before/after lengths without changing the Book. |
| `commit()` | Revalidate, optionally checkpoint, and apply once per resource. |
| `rollback()` | Discard all staged data. |

The transaction implementation supports existing text/binary resources and
staged add/remove/move/rename operations. Binary and structure commits always
require one Checkpoint. A structural commit materializes additions, performs
filesystem relocation with OPF updates suppressed, parses and writes the OPF
once through `ApplyResourceBatch`, applies `UniversalUpdates` to non-OPF
references, then removes staged resources. Relocation cannot be combined with
staged text writes because the latter could overwrite link corrections.

An authoritative package replacement can accompany resource structure
operations. The host verifies that its manifest exactly matches the final
add/remove/move set, that manifest IDs and hrefs are unique, that every spine
`idref` exists, and that the EPUB version is unchanged. In this mode the host
does not generate a second manifest rewrite; it applies the package once after
the physical structure changes. Larger binary resources will use a later chunk
API rather than increasing the JSON message limit.

Archive-file methods cover EPUB entries that Sigil intentionally keeps outside
the `Resource` model, including `mimetype` and files under `META-INF`. Reads use
canonical paths and reject symlinks or paths outside the expanded Book root.
Writes use the SHA-256 returned by the read as their concurrency token and
always require a checkpoint. `mimetype`, `META-INF/container.xml`, the OPF, and
all managed resources are rejected by archive writes; use the resource/package
APIs where applicable.

## Revision and position rules

Resource and Book revisions are unsigned monotonic counters scoped to one live
session. They are concurrency tokens, not persistent identifiers. All writes
require the revision returned by the corresponding read. If content changes in
the meantime, the host returns `RevisionConflict`; a plugin must reread and
recompute rather than overwrite.

Editor positions are UTF-16 code-unit offsets because Qt uses UTF-16
`QTextCursor` positions. Python indexes are Unicode code-point offsets and are
not interchangeable when non-BMP characters occur. Prefer the offsets returned
by `EditorState` and `Selection`; do not derive them with Python `len()` unless
the plugin performs an explicit UTF-16 conversion.

## RPC protocol

The SDK and host use JSON-RPC 2.0 over one private local connection per session:

```text
[4-byte unsigned big-endian length][UTF-8 JSON object]
```

The normal message limit is 8 MiB and the absolute protocol limit is 32 MiB.
The first request must be `session.hello` with protocol version 1, API version
2, plugin name, and the one-time token. The server accepts one client, expires
the token after authentication, and is restricted to the current OS user where
Qt supports it.

The full implemented wire contract is `plugin-api-v2.openrpc.json`.

### Error codes

| Code | Python exception | Meaning |
| --- | --- | --- |
| `-32001` | `PermissionDenied` | Required permission was not granted. |
| `-32002` | `BookClosed` | The Book is no longer available. |
| `-32003` | `ResourceNotFound` | Resource ID or requested text resource is absent. |
| `-32004` | `RevisionConflict` | A write is based on stale content. |
| `-32005` | `InvalidPatch` | Text range or patch shape is invalid. |
| `-32006` | `TransactionRequired` | The requested checkpoint policy is unsafe. |
| `-32007` | `ValidationFailed` | Validation or required Checkpoint failed. |
| `-32008` | `PayloadTooLarge` | Request or response exceeds limits. |
| `-32009` | `Busy` | The session already has an active transaction. |
| `-32010` | `UnsupportedOperation` | The operation is outside the implemented phase. |
| `-32011` | `TransactionNotFound` | Transaction ID is missing, stale, or foreign. |
| `-32012` | `SessionEnding` | The session is shutting down. |

## Security model

The local socket and RPC permissions reduce accidental access to Sigil's Book
model. They are not an operating-system sandbox. A Python plugin runs with the
same user account as Sigil and can use normal filesystem, process, and network
APIs. Install only trusted plugins. Live plugins do not receive the Book root
through this API, but OS permissions still determine what arbitrary Python code
can access.

## Verification

The CTest suite covers frame fragmentation and limits, metadata parsing,
runtime persistence, Python SDK request mapping, stdlib socket transport, a
real launcher handshake, Unicode patch validation, single-step undo, staged
read-your-writes, composed patches, rollback, and application build linkage.
Run it with:

```sh
cmake -S . -B cmake-build-debug -DBUILD_TESTING=ON
cmake --build cmake-build-debug -j2
ctest --test-dir cmake-build-debug --output-on-failure
```
