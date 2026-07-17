import asyncio
import concurrent.futures
import contextlib
import datetime
import functools
import hmac
import json
import os
import secrets
import socket
from typing import Any

import uvicorn
from mcp.server.fastmcp import FastMCP
from mcp.server.fastmcp.exceptions import ToolError
from mcp.server.transport_security import (
    TransportSecurityMiddleware,
    TransportSecuritySettings,
)
from mcp.types import ToolAnnotations
from starlette.routing import Route
from starlette.requests import HTTPConnection

from . import SERVER_NAME, SERVER_VERSION
from .backend import SigilMcpBackend
from .errors import format_tool_error
from .external_import import IMPORT_PATH, create_external_import_handler
from .rendezvous import RendezvousFile


READ_ONLY = ToolAnnotations(
    readOnlyHint=True,
    destructiveHint=False,
    idempotentHint=True,
    openWorldHint=False,
)
UI_STATE = ToolAnnotations(
    readOnlyHint=False,
    destructiveHint=False,
    idempotentHint=True,
    openWorldHint=False,
)
WRITE = ToolAnnotations(
    readOnlyHint=False,
    destructiveHint=True,
    idempotentHint=False,
    openWorldHint=False,
)
STAGE = ToolAnnotations(
    readOnlyHint=False,
    destructiveHint=False,
    idempotentHint=False,
    openWorldHint=False,
)


class LiveCallGate:
    """Keep every Live SDK call on one worker and one request at a time."""

    def __init__(self):
        self._lock = asyncio.Lock()
        self._executor = concurrent.futures.ThreadPoolExecutor(
            max_workers=1, thread_name_prefix="sigil-mcp-live"
        )

    async def call(self, function, *args, **kwargs):
        callback = functools.partial(function, *args, **kwargs)
        async with self._lock:
            loop = asyncio.get_running_loop()
            return await loop.run_in_executor(self._executor, callback)

    def close(self):
        self._executor.shutdown(wait=True, cancel_futures=True)


class BearerTokenMiddleware:
    def __init__(self, app, token):
        self.app = app
        self.token = token.encode("ascii")

    async def __call__(self, scope, receive, send):
        if scope.get("type") != "http":
            await self.app(scope, receive, send)
            return
        headers = {key.lower(): value for key, value in scope.get("headers", [])}
        authorization = headers.get(b"authorization", b"")
        prefix = b"Bearer "
        candidate = (
            authorization[len(prefix):]
            if authorization[:len(prefix)].lower() == prefix.lower()
            else b""
        )
        if not candidate or not hmac.compare_digest(candidate, self.token):
            body = b'{"error":"unauthorized"}'
            await send({
                "type": "http.response.start",
                "status": 401,
                "headers": [
                    (b"content-type", b"application/json"),
                    (b"content-length", str(len(body)).encode("ascii")),
                    (b"www-authenticate", b"Bearer"),
                ],
            })
            await send({"type": "http.response.body", "body": body})
            return
        await self.app(scope, receive, send)


class LoopbackSecurityMiddleware:
    """Apply MCP's Host and Origin validation to non-MCP HTTP endpoints."""

    def __init__(self, app, settings):
        self.app = app
        self.security = TransportSecurityMiddleware(settings)

    async def __call__(self, scope, receive, send):
        if scope.get("type") == "http":
            response = await self.security.validate_request(
                HTTPConnection(scope), is_post=False
            )
            if response is not None:
                await response(scope, receive, send)
                return
        await self.app(scope, receive, send)


async def _invoke(gate, function, *args, **kwargs):
    try:
        return await gate.call(function, *args, **kwargs)
    except Exception as error:
        raise ToolError(format_tool_error(error)) from error


def _json_resource(value):
    return json.dumps(value, ensure_ascii=False, indent=2, sort_keys=True)


def _register_context_tools(mcp, backend, gate):
    @mcp.tool(name="sigil.session.info", annotations=READ_ONLY, structured_output=True)
    async def session_info() -> dict[str, Any]:
        """Return this MCP, Live API, Book session, and active transaction state."""
        return await _invoke(gate, backend.session_info)

    @mcp.tool(name="sigil.capabilities.list", annotations=READ_ONLY, structured_output=True)
    async def capabilities_list() -> dict[str, Any]:
        """List the tools, resources, prompts, limits, and native services actually available."""
        return await _invoke(gate, backend.capabilities)

    @mcp.tool(name="sigil.book.info", annotations=READ_ONLY, structured_output=True)
    async def book_info() -> dict[str, Any]:
        """Return current EPUB version, source path, modified state, and Book revision."""
        return await _invoke(gate, backend.book_info)

    @mcp.tool(name="sigil.book.package", annotations=READ_ONLY, structured_output=True)
    async def book_package() -> dict[str, Any]:
        """Return structured metadata, manifest, spine, guide, and bindings."""
        return await _invoke(gate, backend.book_package)

    @mcp.tool(name="sigil.resource.list", annotations=READ_ONLY, structured_output=True)
    async def resource_list(
        types: list[str] | None = None,
        page_size: int = 100,
        cursor: str | None = None,
    ) -> dict[str, Any]:
        """Return one sorted resource page; pass next_cursor unchanged for the next page."""
        return await _invoke(gate, backend.resource_list, types, page_size, cursor)

    @mcp.tool(name="sigil.resource.read_text", annotations=READ_ONLY, structured_output=True)
    async def resource_read_text(resource_id: str) -> dict[str, Any]:
        """Read current in-memory text and its revision by session-scoped resource ID."""
        return await _invoke(gate, backend.resource_read_text, resource_id)

    @mcp.tool(name="sigil.resource.read_text_range", annotations=READ_ONLY, structured_output=True)
    async def resource_read_text_range(
        resource_id: str,
        start: int = 0,
        max_utf16_units: int = 1024 * 1024,
    ) -> dict[str, Any]:
        """Read a bounded UTF-16 range with total size, revision, SHA-256, and cursor."""
        return await _invoke(
            gate, backend.resource_read_text_range, resource_id, start, max_utf16_units
        )

    @mcp.tool(name="sigil.resource.read_many", annotations=READ_ONLY, structured_output=True)
    async def resource_read_many(
        resource_ids: list[str], cursor: str | None = None
    ) -> dict[str, Any]:
        """Read one bounded page; pass next_cursor unchanged to continue."""
        return await _invoke(gate, backend.resource_read_many, resource_ids, cursor)

    @mcp.tool(name="sigil.editor.state", annotations=READ_ONLY, structured_output=True)
    async def editor_state() -> dict[str, Any]:
        """Return the active editor resource, cursor, selection, UTF-16 encoding, and revision."""
        return await _invoke(gate, backend.editor_state)

    @mcp.tool(name="sigil.editor.tabs", annotations=READ_ONLY, structured_output=True)
    async def editor_tabs() -> dict[str, Any]:
        """Return resources currently loaded in editor tabs."""
        return await _invoke(gate, backend.editor_tabs)


def _register_editor_tools(mcp, backend, gate):
    @mcp.tool(name="sigil.editor.open", annotations=UI_STATE, structured_output=True)
    async def editor_open(resource_id: str, position: int | None = None) -> dict[str, Any]:
        """Open a resource and optionally reveal a UTF-16 position without editing content."""
        return await _invoke(gate, backend.editor_open, resource_id, position)

    @mcp.tool(name="sigil.editor.reveal", annotations=UI_STATE, structured_output=True)
    async def editor_reveal(resource_id: str, start: int, end: int) -> dict[str, Any]:
        """Open a resource and select a validated UTF-16 range."""
        return await _invoke(gate, backend.editor_reveal, resource_id, start, end)

    @mcp.tool(name="sigil.editor.edit", annotations=WRITE, structured_output=True)
    async def editor_edit(
        resource_id: str,
        expected_revision: int,
        edits: list[dict],
        label: str = "MCP editor edit",
    ) -> dict[str, Any]:
        """Apply non-overlapping UTF-16 edits immediately as one editor undo step."""
        return await _invoke(
            gate, backend.editor_edit, resource_id, expected_revision, edits, label
        )

    @mcp.tool(name="sigil.editor.replace_selection", annotations=WRITE, structured_output=True)
    async def editor_replace_selection(
        resource_id: str,
        expected_revision: int,
        expected_state_token: str,
        text: str,
        label: str = "MCP replace selection",
    ) -> dict[str, Any]:
        """Replace the active selection after checking resource, revision, and state token."""
        return await _invoke(
            gate,
            backend.editor_replace_selection,
            resource_id,
            expected_revision,
            expected_state_token,
            text,
            label,
        )

    @mcp.tool(name="sigil.editor.insert_text", annotations=WRITE, structured_output=True)
    async def editor_insert_text(
        resource_id: str,
        expected_revision: int,
        expected_state_token: str,
        text: str,
        label: str = "MCP insert text",
    ) -> dict[str, Any]:
        """Insert text at the active cursor after checking resource, revision, and state token."""
        return await _invoke(
            gate,
            backend.editor_insert_text,
            resource_id,
            expected_revision,
            expected_state_token,
            text,
            label,
        )


def _register_transaction_tools(mcp, backend, gate):
    @mcp.tool(name="sigil.transaction.begin", annotations=STAGE, structured_output=True)
    async def transaction_begin(
        label: str = "MCP changes", checkpoint: str = "auto"
    ) -> dict[str, Any]:
        """Acquire the Book writer lease and begin one staged transaction."""
        return await _invoke(gate, backend.transaction_begin, label, checkpoint)

    @mcp.tool(name="sigil.transaction.status", annotations=READ_ONLY, structured_output=True)
    async def transaction_status() -> dict[str, Any]:
        """Inspect the active transaction and idle expiry state without requiring its handle."""
        return await _invoke(gate, backend.transaction_status)

    @mcp.tool(name="sigil.transaction.read_text", annotations=READ_ONLY, structured_output=True)
    async def transaction_read_text(transaction_id: str, resource_id: str) -> dict[str, Any]:
        """Read transaction-staged text when present, otherwise current live text."""
        return await _invoke(
            gate, backend.transaction_read_text, transaction_id, resource_id
        )

    @mcp.tool(name="sigil.transaction.read_text_range", annotations=READ_ONLY, structured_output=True)
    async def transaction_read_text_range(
        transaction_id: str,
        resource_id: str,
        start: int = 0,
        max_utf16_units: int = 1024 * 1024,
    ) -> dict[str, Any]:
        """Read a bounded range from live, staged, or newly staged text."""
        return await _invoke(
            gate,
            backend.transaction_read_text_range,
            transaction_id,
            resource_id,
            start,
            max_utf16_units,
        )

    @mcp.tool(name="sigil.transaction.replace_text", annotations=STAGE, structured_output=True)
    async def transaction_replace_text(
        transaction_id: str,
        resource_id: str,
        expected_revision: int,
        text: str,
    ) -> dict[str, Any]:
        """Stage a complete text replacement against the transaction revision."""
        return await _invoke(
            gate,
            backend.transaction_replace_text,
            transaction_id,
            resource_id,
            expected_revision,
            text,
        )

    @mcp.tool(name="sigil.transaction.apply_edits", annotations=STAGE, structured_output=True)
    async def transaction_apply_edits(
        transaction_id: str,
        resource_id: str,
        expected_revision: int,
        edits: list[dict],
    ) -> dict[str, Any]:
        """Compose non-overlapping UTF-16 edits against the staged resource text."""
        return await _invoke(
            gate,
            backend.transaction_apply_edits,
            transaction_id,
            resource_id,
            expected_revision,
            edits,
        )

    @mcp.tool(name="sigil.transaction.begin_text_write", annotations=STAGE, structured_output=True)
    async def transaction_begin_text_write(
        transaction_id: str,
        resource_id: str,
        expected_revision: int,
        size: int,
    ) -> dict[str, Any]:
        """Begin a chunked UTF-8 replacement of existing or newly staged text."""
        return await _invoke(
            gate,
            backend.transaction_begin_text_write,
            transaction_id,
            resource_id,
            expected_revision,
            size,
        )

    @mcp.tool(name="sigil.transaction.begin_text_resource", annotations=STAGE, structured_output=True)
    async def transaction_begin_text_resource(
        transaction_id: str,
        book_path: str,
        size: int,
        media_type: str,
        manifest_id: str | None = None,
        properties: str | None = None,
        add_to_spine: bool = True,
        manifested: bool = True,
    ) -> dict[str, Any]:
        """Begin a chunked UTF-8 addition and return a resumable upload handle."""
        return await _invoke(
            gate,
            backend.transaction_begin_text_resource,
            transaction_id,
            book_path,
            size,
            media_type,
            manifest_id,
            properties,
            add_to_spine,
            manifested,
        )

    @mcp.tool(name="sigil.transaction.write_text_chunk", annotations=STAGE, structured_output=True)
    async def transaction_write_text_chunk(
        transaction_id: str,
        upload_id: str,
        offset: int,
        text: str,
    ) -> dict[str, Any]:
        """Append one idempotent UTF-8 text chunk at the declared byte offset."""
        return await _invoke(
            gate,
            backend.transaction_write_text_chunk,
            transaction_id,
            upload_id,
            offset,
            text,
        )

    @mcp.tool(name="sigil.transaction.finish_text_write", annotations=STAGE, structured_output=True)
    async def transaction_finish_text_write(
        transaction_id: str, upload_id: str
    ) -> dict[str, Any]:
        """Verify length, UTF-8, and SHA-256, then stage the completed text."""
        return await _invoke(
            gate, backend.transaction_finish_text_write, transaction_id, upload_id
        )

    @mcp.tool(name="sigil.transaction.abort_text_write", annotations=STAGE, structured_output=True)
    async def transaction_abort_text_write(
        transaction_id: str, upload_id: str
    ) -> dict[str, Any]:
        """Discard an unfinished chunked text upload without ending the transaction."""
        return await _invoke(
            gate, backend.transaction_abort_text_write, transaction_id, upload_id
        )

    @mcp.tool(name="sigil.transaction.add_text_resource", annotations=STAGE, structured_output=True)
    async def transaction_add_text_resource(
        transaction_id: str,
        book_path: str,
        text: str,
        media_type: str,
        manifest_id: str | None = None,
        properties: str | None = None,
        add_to_spine: bool = True,
        manifested: bool = True,
    ) -> dict[str, Any]:
        """Stage a generated text resource at a canonical Book path."""
        return await _invoke(
            gate,
            backend.transaction_add_text_resource,
            transaction_id,
            book_path,
            text,
            media_type,
            manifest_id,
            properties,
            add_to_spine,
            manifested,
        )

    @mcp.tool(name="sigil.transaction.add_binary_resource", annotations=STAGE, structured_output=True)
    async def transaction_add_binary_resource(
        transaction_id: str,
        book_path: str,
        data_base64: str,
        media_type: str,
        manifest_id: str | None = None,
        properties: str | None = None,
        add_to_spine: bool = False,
        manifested: bool = True,
    ) -> dict[str, Any]:
        """Stage a strict-Base64 binary resource up to the inline size limit."""
        return await _invoke(
            gate,
            backend.transaction_add_binary_resource,
            transaction_id,
            book_path,
            data_base64,
            media_type,
            manifest_id,
            properties,
            add_to_spine,
            manifested,
        )

    @mcp.tool(name="sigil.transaction.remove_resource", annotations=WRITE, structured_output=True)
    async def transaction_remove_resource(
        transaction_id: str, resource_id: str, expected_revision: int
    ) -> dict[str, Any]:
        """Stage resource deletion subject to protected-resource and package checks."""
        return await _invoke(
            gate,
            backend.transaction_remove_resource,
            transaction_id,
            resource_id,
            expected_revision,
        )

    @mcp.tool(name="sigil.transaction.move_resource", annotations=WRITE, structured_output=True)
    async def transaction_move_resource(
        transaction_id: str,
        resource_id: str,
        book_path: str,
        expected_revision: int,
    ) -> dict[str, Any]:
        """Stage a resource move and reference updates to a canonical Book path."""
        return await _invoke(
            gate,
            backend.transaction_move_resource,
            transaction_id,
            resource_id,
            book_path,
            expected_revision,
        )

    @mcp.tool(name="sigil.transaction.rename_resource", annotations=WRITE, structured_output=True)
    async def transaction_rename_resource(
        transaction_id: str,
        resource_id: str,
        filename: str,
        expected_revision: int,
    ) -> dict[str, Any]:
        """Stage a filename change in the resource's current Book directory."""
        return await _invoke(
            gate,
            backend.transaction_rename_resource,
            transaction_id,
            resource_id,
            filename,
            expected_revision,
        )

    @mcp.tool(name="sigil.transaction.replace_package", annotations=WRITE, structured_output=True)
    async def transaction_replace_package(
        transaction_id: str, text: str, expected_revision: int
    ) -> dict[str, Any]:
        """Stage an authoritative OPF replacement after structured package validation."""
        return await _invoke(
            gate,
            backend.transaction_replace_package,
            transaction_id,
            text,
            expected_revision,
        )

    @mcp.tool(name="sigil.transaction.update_metadata", annotations=STAGE, structured_output=True)
    async def transaction_update_metadata(
        transaction_id: str,
        items: list[dict],
        expected_revision: int | None = None,
    ) -> dict[str, Any]:
        """Stage replacement of ordered structured OPF metadata entries."""
        return await _invoke(
            gate,
            backend.transaction_update_metadata,
            transaction_id,
            items,
            expected_revision,
        )

    @mcp.tool(name="sigil.transaction.update_spine", annotations=STAGE, structured_output=True)
    async def transaction_update_spine(
        transaction_id: str,
        items: list[dict],
        attributes: dict | None = None,
        expected_revision: int | None = None,
    ) -> dict[str, Any]:
        """Stage replacement of ordered OPF spine itemrefs and attributes."""
        return await _invoke(
            gate,
            backend.transaction_update_spine,
            transaction_id,
            items,
            attributes,
            expected_revision,
        )

    @mcp.tool(name="sigil.transaction.preview", annotations=READ_ONLY, structured_output=True)
    async def transaction_preview(transaction_id: str) -> dict[str, Any]:
        """Return staged text, structure, OPF, size, warning, and conflict summaries."""
        return await _invoke(gate, backend.transaction_preview, transaction_id)

    @mcp.tool(name="sigil.transaction.validate", annotations=READ_ONLY, structured_output=True)
    async def transaction_validate(transaction_id: str) -> dict[str, Any]:
        """Validate revisions and EPUB/package invariants without changing the Book."""
        return await _invoke(gate, backend.transaction_validate, transaction_id)

    @mcp.tool(name="sigil.transaction.commit", annotations=WRITE, structured_output=True)
    async def transaction_commit(transaction_id: str) -> dict[str, Any]:
        """Revalidate, checkpoint as needed, and commit without a confirmation dialog."""
        return await _invoke(gate, backend.transaction_commit, transaction_id)

    @mcp.tool(name="sigil.transaction.rollback", annotations=STAGE, structured_output=True)
    async def transaction_rollback(transaction_id: str) -> dict[str, Any]:
        """Discard all staged changes and release the Book writer lease."""
        return await _invoke(gate, backend.transaction_rollback, transaction_id)


def _register_resources(mcp, backend, gate):
    @mcp.resource("sigil://book/info", mime_type="application/json")
    async def book_info_resource() -> str:
        return _json_resource(await _invoke(gate, backend.book_info))

    @mcp.resource("sigil://book/metadata", mime_type="application/json")
    async def book_metadata_resource() -> str:
        package = await _invoke(gate, backend.book_package)
        return _json_resource(package["metadata"])

    @mcp.resource("sigil://book/manifest", mime_type="application/json")
    async def book_manifest_resource() -> str:
        package = await _invoke(gate, backend.book_package)
        return _json_resource(package["manifest"])

    @mcp.resource("sigil://book/spine", mime_type="application/json")
    async def book_spine_resource() -> str:
        package = await _invoke(gate, backend.book_package)
        return _json_resource(package["spine"])

    @mcp.resource("sigil://editor/state", mime_type="application/json")
    async def editor_state_resource() -> str:
        return _json_resource(await _invoke(gate, backend.editor_state))

    @mcp.resource("sigil://resource/{resource_id}", mime_type="text/plain")
    async def text_resource(resource_id: str) -> str:
        result = await _invoke(gate, backend.resource_read_text, resource_id)
        return result["text"]


def _register_prompts(mcp):
    @mcp.prompt(name="edit_epub_safely")
    def edit_epub_safely(task: str) -> str:
        return (
            "Perform this EPUB task safely: {0}\n"
            "Inspect sigil.book.info and the relevant resources first. Use editor tools "
            "only for a small current-tab edit. For multi-file, OPF, generated chapter, "
            "or layout work, begin one transaction, read transaction content, stage "
            "revision-checked changes, preview, validate, and commit only after review. "
            "On conflicts reread and replan; never guess resource IDs or paths."
        ).format(task)

    @mcp.prompt(name="generate_chapter")
    def generate_chapter(topic: str, book_path: str) -> str:
        return (
            "Generate an EPUB-valid XHTML chapter about {0} at {1}. Inspect the Book's "
            "EPUB version, nearby XHTML conventions, manifest, spine, language, and CSS. "
            "Use sigil.transaction.add_text_resource, preview and validate the package, "
            "then request commit. Do not create duplicate manifest IDs or external assets."
        ).format(topic, book_path)

    @mcp.prompt(name="layout_epub")
    def layout_epub(goal: str) -> str:
        return (
            "Improve EPUB layout for this goal: {0}. Read current XHTML and CSS before "
            "changing them. Prefer semantic markup, reusable CSS, relative resource paths, "
            "and EPUB-version-compatible properties. Stage all related XHTML/CSS/package "
            "changes in one transaction, preview, validate, and preserve accessibility."
        ).format(goal)

    @mcp.prompt(name="repair_epub")
    def repair_epub(issue: str) -> str:
        return (
            "Repair this EPUB issue: {0}. Reproduce it from current Book/package data, "
            "identify every affected resource, and make the smallest revision-checked "
            "transaction. Preview and validate before commit. If validation or revision "
            "checks fail, do not overwrite; reread and revise the plan."
        ).format(issue)


def _transport_security_settings():
    return TransportSecuritySettings(
        enable_dns_rebinding_protection=True,
        allowed_hosts=["127.0.0.1:*", "localhost:*", "[::1]:*"],
        allowed_origins=[
            "http://127.0.0.1:*",
            "http://localhost:*",
            "https://127.0.0.1:*",
            "https://localhost:*",
        ],
    )


def create_mcp(backend, gate, port):
    security = _transport_security_settings()
    mcp = FastMCP(
        SERVER_NAME,
        instructions=(
            "Operate only on the currently open Sigil Book. Read current revisions before "
            "editing and use a previewed, validated transaction for multi-resource changes."
        ),
        host="127.0.0.1",
        port=port,
        streamable_http_path="/mcp",
        json_response=True,
        stateless_http=False,
        log_level="WARNING",
        transport_security=security,
    )
    _register_context_tools(mcp, backend, gate)
    _register_editor_tools(mcp, backend, gate)
    _register_transaction_tools(mcp, backend, gate)
    _register_resources(mcp, backend, gate)
    _register_prompts(mcp)
    return mcp


def create_http_app(mcp, backend, gate, token):
    app = mcp.streamable_http_app()
    app.routes.append(
        Route(
            IMPORT_PATH,
            endpoint=create_external_import_handler(backend, gate),
            methods=["POST"],
        )
    )
    app.add_middleware(
        LoopbackSecurityMiddleware, settings=_transport_security_settings()
    )
    app.add_middleware(BearerTokenMiddleware, token=token)
    return app


def _loopback_socket():
    listener = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    listener.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    listener.set_inheritable(False)
    listener.bind(("127.0.0.1", 0))
    listener.listen(socket.SOMAXCONN)
    return listener


async def _monitor_host(plugin, backend, gate, server):
    while not server.should_exit:
        await asyncio.sleep(2)
        try:
            await gate.call(plugin.ping)
            await gate.call(backend.expire_idle_transaction)
        except (ConnectionError, OSError, RuntimeError):
            server.should_exit = True
            return


async def _run_server(plugin):
    idle_timeout = int(os.environ.get("SIGIL_MCP_TRANSACTION_TIMEOUT", "300"))
    backend = SigilMcpBackend(plugin, idle_timeout_seconds=idle_timeout)
    gate = LiveCallGate()
    listener = _loopback_socket()
    port = listener.getsockname()[1]
    token = secrets.token_urlsafe(32)
    rendezvous = RendezvousFile(plugin.session_info)
    mcp = create_mcp(backend, gate, port)
    app = create_http_app(mcp, backend, gate, token)
    config = uvicorn.Config(
        app,
        host="127.0.0.1",
        port=port,
        log_level="warning",
        access_log=False,
    )
    server = uvicorn.Server(config)
    server_task = asyncio.create_task(server.serve(sockets=[listener]))
    monitor_task = None
    try:
        for _ in range(200):
            if server.started:
                break
            if server_task.done():
                await server_task
            await asyncio.sleep(0.01)
        if not server.started:
            raise RuntimeError("MCP HTTP server did not start")

        book = await gate.call(backend.book_info)
        metadata_path = rendezvous.write({
            "schema_version": 1,
            "server": {"name": SERVER_NAME, "version": SERVER_VERSION},
            "transport": "streamable-http",
            "endpoint": "http://127.0.0.1:{0}/mcp".format(port),
            "external_import_endpoint": "http://127.0.0.1:{0}{1}".format(
                port, IMPORT_PATH
            ),
            "token_type": "Bearer",
            "token": token,
            "pid": os.getpid(),
            "session_id": plugin.session_info.get("session_id"),
            "book": {
                "file_path": book.get("file_path"),
                "epub_version": book.get("epub_version"),
                "revision": book.get("revision"),
            },
            "created_at": datetime.datetime.now(datetime.timezone.utc).isoformat(),
        })
        await gate.call(
            plugin.ui.show_status,
            "Sigil MCP Server is listening; connection metadata: {0}".format(metadata_path),
            10000,
        )
        monitor_task = asyncio.create_task(_monitor_host(plugin, backend, gate, server))
        await server_task
        return 0
    finally:
        server.should_exit = True
        if monitor_task is not None:
            monitor_task.cancel()
            with contextlib.suppress(asyncio.CancelledError):
                await monitor_task
        if not server_task.done():
            with contextlib.suppress(Exception):
                await server_task
        with contextlib.suppress(Exception):
            await gate.call(backend.shutdown)
        rendezvous.remove()
        with contextlib.suppress(OSError):
            listener.close()
        gate.close()


def run_server(plugin):
    try:
        return asyncio.run(_run_server(plugin))
    except Exception as error:
        plugin.ui.show_message(
            "The MCP server stopped: {0}".format(error),
            "Sigil MCP Server",
            level="error",
        )
        return 1
