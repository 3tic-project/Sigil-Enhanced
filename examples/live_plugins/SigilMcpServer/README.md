# Sigil MCP Server

This installable Live API v2 book-session plugin exposes the currently open,
in-memory EPUB through Model Context Protocol (MCP).

## Install

From the repository root, build the plugin archive with:

```console
python3 examples/live_plugins/package_plugin.py examples/live_plugins/SigilMcpServer
```

Install the generated `examples/live_plugins/SigilMcpServer.zip` with Sigil's
Plugin Manager. Do not select this source directory or use Finder's generic
compression command: the manager accepts ZIP files whose only top-level
directory matches the ZIP basename. Open one EPUB and run **Sigil MCP Server**.
The Plugin Session Console remains active for the Book lifetime.

The status bar reports the generated `sigil-mcp-<session>.json` metadata path.
That owner-only file contains a dynamic loopback endpoint and bearer token. Do
not paste the token into logs, EPUB content, issue reports, or source files.

## Connect

Streamable HTTP clients use the metadata `endpoint` and this header:

```text
Authorization: Bearer <metadata token>
```

For a stdio-only MCP Host, configure Python 3 to run the bundled
`sigil_mcp_stdio_proxy.py`. With one active Book, the proxy discovers it. With
multiple Books, pass `--metadata <exact-json-path>` or `--session-id <id>`; the
proxy deliberately refuses to guess.

The proxy is installed with Sigil's Python plugin launcher files and also lives
in this source tree at:

```text
src/Resource_Files/plugin_launchers/python/sigil_mcp_stdio_proxy.py
```

## Write Workflow

Use immediate editor tools only for a small active-tab change. For generated
chapters, CSS/layout changes, OPF metadata, spine work, or multiple resources:

1. Read current Book/package/resource state and revisions.
2. Call `sigil.transaction.begin`.
3. Stage changes using the returned `transaction_id`.
4. Call `sigil.transaction.preview` and `sigil.transaction.validate`.
5. Call `sigil.transaction.commit`; Sigil displays a native summary confirmation.
6. On rejection, conflict, or changed intent, call `sigil.transaction.rollback`.

Uncommitted transactions roll back after five idle minutes by default, when the
server stops, or when the Book session ends.

## Documentation

- `docs/MCPUserGuide_zh-CN.md`
- `docs/MCPToolsReference_zh-CN.md`
- `docs/MCPSystem.md`
- `docs/LivePythonPluginAPI.md`
