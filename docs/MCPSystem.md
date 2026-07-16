# Sigil Enhanced MCP System

## Status

- Architecture baseline: 2026-07-16
- Sigil baseline: 2.8.1E5 (`6db30a158`)
- MCP specification: 2025-11-25
- Python SDK: `mcp>=1.28.1,<2`
- Delivery model: installable Live Python Plugin API v2 book-session plugin

This document is the tracked implementation contract for the Sigil Enhanced MCP
adapter. The longer research draft remains in
`todo/Sigil-Enhanced-MCP-System-Design.md` for development planning.

## Goal

Expose the current in-memory EPUB and editor state to an MCP Host so an LLM can:

- inspect book structure, metadata, resources, open tabs, cursor, and selection;
- read unsaved XHTML, CSS, XML, OPF, NCX, SVG, and text resources;
- navigate the editor and make revision-checked, undoable local edits;
- generate or replace chapters, stylesheets, navigation files, and metadata;
- stage multi-file editing and layout work in a validated transaction;
- preview, validate, commit, or roll back changes without bypassing Sigil;
- use future native enhancement services such as source formatting, Chinese
  conversion, structure normalization, and font subsetting.

The MCP adapter is not an autonomous LLM client. It never calls a model itself.

## Researched Baseline

The stable MCP specification defines newline-delimited stdio and Streamable HTTP
as its standard transports. A Streamable HTTP server must expose one endpoint,
validate `Origin` when present, bind locally when used on one machine, and
authenticate connections. The protocol uses JSON-RPC lifecycle negotiation and
separates model-controlled tools, application-controlled resources, and
user-controlled prompts.

As of 2026-07-16, the official Python SDK 1.28.1 is the latest stable release.
The 2.x line is still pre-release, so the bundled dependency has an explicit
`<2` upper bound. Upgrading to 2.x requires a separate compatibility change and
cross-host regression pass.

Primary references:

- <https://modelcontextprotocol.io/specification/2025-11-25/basic/lifecycle>
- <https://modelcontextprotocol.io/specification/2025-11-25/basic/transports>
- <https://modelcontextprotocol.io/specification/2025-11-25/server/tools>
- <https://modelcontextprotocol.io/specification/2025-11-25/server/resources>
- <https://modelcontextprotocol.io/specification/2025-11-25/server/prompts>
- <https://github.com/modelcontextprotocol/python-sdk/tree/v1.x>

## Architecture

```text
MCP Host / coding agent
        |
        | Streamable HTTP on 127.0.0.1 + bearer token
        v
Sigil MCP adapter (Python, one book-session per open Book)
        |
        | serialized sigil_live calls
        v
Live Plugin API v2 / PluginSession
        |
        +-- current Book and editor memory
        +-- UTF-16 revision-checked editor edits
        +-- staged Book transaction and writer lease
        +-- OPF/package validation and Checkpoint
        +-- compensating rollback
```

The Python adapter owns MCP lifecycle, schemas, transport, resources, prompts,
and error mapping. It must not read the expanded EPUB directory directly, parse
and overwrite the OPF independently, or mutate arbitrary local files.

The C++ host remains the only authority for Book identity, resource revisions,
editor undo, writer locking, package invariants, Checkpoints, and commit.

## Transport And Discovery

The first release uses Streamable HTTP only:

- bind exactly to `127.0.0.1` on a pre-bound dynamic port;
- expose `/mcp` with JSON responses;
- enable the official SDK's Host and Origin validation;
- accept absent Origin for native clients and allow only loopback HTTP origins;
- require a randomly generated bearer token using constant-time comparison;
- write endpoint metadata atomically with owner-only permissions;
- place metadata in the per-user runtime directory supplied by the host;
- use one metadata file per Book session and never silently choose among Books;
- remove owned metadata on orderly shutdown;
- stop on Book close, plugin cancellation, or Live API heartbeat failure.

The metadata contains the endpoint, token, process ID, session ID, Book identity,
protocol version, and creation time. It is a local secret and must not be logged,
returned by a tool, stored in an EPUB, or committed to source control.

A stdio proxy is deferred. It will only translate the standard transport and
discover an explicitly selected Book endpoint; it will contain no EPUB logic.

## Concurrency

`sigil_live` uses one synchronous `QLocalSocket` connection. All MCP tool and
resource calls therefore enter a single-worker call gate. The HTTP layer may
receive concurrent requests, but it may not call the Live SDK concurrently.

Each endpoint permits one active write transaction. It is addressed by the
explicit `transaction_id` returned from begin. The shared endpoint token does not
identify separate clients, so the initial implementation does not claim
per-client transaction isolation. A future native coordinator can add client
identity and routing.

## Tool Groups

Tool names are stable and grouped under `sigil.*`.

### Session And Context

- `sigil.session.info`
- `sigil.capabilities.list`
- `sigil.book.info`
- `sigil.book.package`
- `sigil.resource.list`
- `sigil.resource.read_text`
- `sigil.resource.read_many`
- `sigil.editor.state`
- `sigil.editor.tabs`

### Editor

- `sigil.editor.open`
- `sigil.editor.reveal`
- `sigil.editor.edit`
- `sigil.editor.replace_selection`
- `sigil.editor.insert_text`

Editor offsets use UTF-16 code units. Every content edit requires a resource ID
and expected revision and becomes one Qt undo step.

### Transaction

- `sigil.transaction.begin`
- `sigil.transaction.read_text`
- `sigil.transaction.replace_text`
- `sigil.transaction.apply_edits`
- `sigil.transaction.add_text_resource`
- `sigil.transaction.remove_resource`
- `sigil.transaction.move_resource`
- `sigil.transaction.rename_resource`
- `sigil.transaction.replace_package`
- `sigil.transaction.update_metadata`
- `sigil.transaction.update_spine`
- `sigil.transaction.preview`
- `sigil.transaction.validate`
- `sigil.transaction.commit`
- `sigil.transaction.rollback`

Staging does not modify the live Book. Read-your-writes, revision conflict
detection, package validation, Checkpoint policy, and rollback come from the Live
API. Commit displays a native Sigil confirmation based on the transaction
preview. Rejecting confirmation keeps the transaction available for inspection
or rollback.

## Resources And Prompts

Stable read-only resources:

- `sigil://book/info`
- `sigil://book/metadata`
- `sigil://book/manifest`
- `sigil://book/spine`
- `sigil://editor/state`
- `sigil://resource/{resource_id}`

Workflow prompts:

- `edit_epub_safely`
- `generate_chapter`
- `layout_epub`
- `repair_epub`

Prompts describe safe workflows; they are not permissions and cannot weaken host
validation.

## Security Boundary

The adapter is trusted local plugin code, not a sandbox. The bearer token protects
the loopback MCP endpoint from unrelated local/web clients but does not restrict
what the installed Python plugin process can do.

The adapter does not expose:

- arbitrary QAction execution;
- Python evaluation or shell commands;
- arbitrary local path reads/writes;
- public-network listening;
- direct EPUB directory access;
- output export to a model-selected path;
- a replacement plugin permission system.

MCP tool annotations are hints for Host approval UX. Real enforcement remains in
the Live API's revision, resource, path, transaction, and plugin-type checks.

## Error Contract

Expected Live API failures are returned as MCP tool errors with a JSON object:

```json
{
  "code": "RevisionConflict",
  "message": "Resource changed since it was read",
  "retryable": true,
  "recovery": "Read the resource again and rebuild the edit",
  "data": {}
}
```

Stable codes include `BookClosed`, `ResourceNotFound`, `RevisionConflict`,
`InvalidPatch`, `ValidationFailed`, `PayloadTooLarge`, `Busy`,
`TransactionNotFound`, and `SessionEnding`.

## Native Enhancement Roadmap

Generic MCP editing is sufficient for LLM-generated text, chapters, CSS, and
metadata. Native algorithms must be exposed later through explicit structured
services rather than copied into the adapter:

1. EPUB-safe XHTML/CSS source formatter;
2. structure normalizer and validation results;
3. structure-safe Chinese conversion plans;
4. HarfBuzz font subsetting plans.

Each service will use analyze/preview/commit/discard plan semantics, bind plans to
Book and revision, and share its core implementation with the GUI workflow.

## Verification

The tracked test suite must cover:

- deterministic tool names and JSON Schemas;
- MCP initialize, tools/list, resources/list/read, prompts/list/get, and tools/call;
- bearer, Host, Origin, and loopback behavior;
- resource pagination and current in-memory text;
- UTF-16 editor edits and revision errors;
- transaction state, preview, validation, confirmation, commit, and rollback;
- automatic rollback on shutdown and failed heartbeat;
- atomic rendezvous creation, permissions, stale-file cleanup, and token secrecy;
- package dependency synchronization for macOS and Windows builds.

Passing unit and local integration tests does not prove cross-platform or every
Host compatibility. Release readiness still requires MCP Inspector plus at least
two real Hosts on Windows, macOS, and Linux.
