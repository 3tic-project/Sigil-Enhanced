import base64
import binascii
import dataclasses
import hashlib
import pathlib
import time

from .catalog import PROMPT_NAMES, RESOURCE_URIS, TOOL_NAMES
from .errors import BackendError
from . import MCP_PROTOCOL_VERSION, SERVER_NAME, SERVER_VERSION


MAX_INLINE_BINARY_SIZE = 5 * 1024 * 1024
MAX_EXTERNAL_IMPORT_SIZE = 32 * 1024 * 1024
MAX_EXTERNAL_IMPORTS = 2


def _resource(value):
    return dataclasses.asdict(value)


def _selection(value):
    return dataclasses.asdict(value) if value is not None else None


def _editor_state(value):
    result = dataclasses.asdict(value)
    result["selection"] = _selection(value.selection)
    return result


class SigilMcpBackend:
    def __init__(self, plugin, idle_timeout_seconds=300):
        self.plugin = plugin
        self.idle_timeout_seconds = max(60, min(int(idle_timeout_seconds), 1800))
        self._transaction = None
        self._transaction_activity = None
        self._text_writers = {}
        self._external_imports = 0

    def session_info(self):
        info = dict(self.plugin.session_info)
        return {
            "server": {"name": SERVER_NAME, "version": SERVER_VERSION},
            "mcp_protocol": MCP_PROTOCOL_VERSION,
            "live_api": {
                "api_version": info.get("api_version"),
                "protocol_version": info.get("protocol_version"),
                "position_encoding": info.get("position_encoding"),
                "max_message_size": info.get("max_message_size"),
            },
            "session": {
                "session_id": info.get("session_id"),
                "lifetime": info.get("lifetime"),
                "transaction_active": self._transaction is not None,
                "transaction_idle_timeout_seconds": self.idle_timeout_seconds,
            },
            "book": self.plugin.book.get_info(),
        }

    def capabilities(self):
        binary_add_max = self._external_binary_add_max()
        return {
            "tools": list(TOOL_NAMES),
            "resources": list(RESOURCE_URIS),
            "prompts": list(PROMPT_NAMES),
            "limits": {
                "resource_page_size_max": 500,
                "read_many_max": 100,
                "text_range_utf16_units_max": 1024 * 1024,
                "text_write_size_max": 64 * 1024 * 1024,
                "text_write_chunk_size_max": 1024 * 1024,
                "inline_binary_size_max": MAX_INLINE_BINARY_SIZE,
                "external_import_size_max": MAX_EXTERNAL_IMPORT_SIZE,
                "external_binary_add_size_max": binary_add_max,
                "editor_edits_max": 1000,
                "position_encoding": "utf-16",
                "active_transactions": 1,
                "transaction_idle_timeout_seconds": self.idle_timeout_seconds,
            },
            "external_import": {
                "available": True,
                "endpoint_path": "/api/v1/imports",
                "authentication": "session bearer token",
                "content": "raw bytes",
                "operations": ["add", "replace"],
                "kinds": ["text", "binary"],
                "binary_add_size_max": binary_add_max,
                "batch_uploader": "sigil_mcp_upload.py",
            },
            "enhancements": [],
        }

    def book_info(self):
        return self.plugin.book.get_info()

    def book_package(self):
        return {
            "metadata": self.plugin.book.get_metadata(),
            "manifest": self.plugin.book.get_manifest(),
            "spine": self.plugin.book.get_spine(),
            "guide": self.plugin.book.get_guide(),
            "bindings": self.plugin.book.get_bindings(),
        }

    def resource_list(self, types=None, page_size=100, cursor=None):
        if not 1 <= page_size <= 500:
            raise BackendError(
                "InvalidRequest", "page_size must be between 1 and 500"
            )
        page = self.plugin.book.list_resources(
            types=types, page_size=page_size, cursor=cursor
        )
        return {
            "items": [_resource(item) for item in page["items"]],
            "next_cursor": page["next_cursor"],
        }

    def resource_read_text(self, resource_id):
        result = dict(self.plugin.book.read_text(resource_id))
        result["resource_id"] = resource_id
        return result

    def resource_read_text_range(self, resource_id, start=0, max_utf16_units=1024 * 1024):
        return self.plugin.book.read_text_range(resource_id, start, max_utf16_units)

    def resource_read_many(self, resource_ids, cursor=None):
        if not 1 <= len(resource_ids) <= 100:
            raise BackendError(
                "InvalidRequest", "resource_ids must contain between 1 and 100 IDs"
            )
        return self.plugin.book.read_many_page(resource_ids, cursor)

    def editor_state(self):
        return _editor_state(self.plugin.editor.get_state())

    def editor_tabs(self):
        return {"items": [_resource(item) for item in self.plugin.editor.get_open_tabs()]}

    def editor_open(self, resource_id, position=None):
        return _editor_state(self.plugin.editor.open_resource(resource_id, position))

    def editor_reveal(self, resource_id, start, end):
        return _editor_state(self.plugin.editor.reveal_range(resource_id, start, end))

    def editor_edit(self, resource_id, expected_revision, edits, label="MCP editor edit"):
        return self.plugin.editor.apply_edits(
            edits,
            expected_revision=expected_revision,
            resource_id=resource_id,
            label=label,
        )

    def editor_replace_selection(
        self, resource_id, expected_revision, expected_state_token, text,
        label="MCP replace selection"
    ):
        return self.plugin.editor.replace_selection(
            text,
            expected_revision=expected_revision,
            resource_id=resource_id,
            label=label,
            expected_state_token=expected_state_token,
        )

    def editor_insert_text(
        self, resource_id, expected_revision, expected_state_token, text,
        label="MCP insert text"
    ):
        return self.plugin.editor.insert_text(
            text,
            expected_revision=expected_revision,
            resource_id=resource_id,
            label=label,
            expected_state_token=expected_state_token,
        )

    def transaction_begin(self, label="MCP changes", checkpoint="auto"):
        if self._transaction is not None:
            raise BackendError(
                "Busy",
                "This Book endpoint already has an active transaction",
                retryable=True,
                recovery="Commit or roll back the current transaction first.",
            )
        self._transaction = self.plugin.book.transaction(label, checkpoint)
        self._touch_transaction()
        return self.transaction_status()

    def transaction_status(self):
        if self._transaction is None:
            return {
                "active": False,
                "transaction_id": None,
                "idle_timeout_seconds": self.idle_timeout_seconds,
                "idle_seconds": None,
                "expires_in_seconds": None,
                "pending_text_uploads": 0,
                "pending_external_imports": 0,
            }
        idle_seconds = max(0.0, time.monotonic() - self._transaction_activity)
        return {
            "active": True,
            "transaction_id": self._transaction.id,
            "base_book_revision": self._transaction.base_book_revision,
            "checkpoint": self._transaction.checkpoint,
            "idle_timeout_seconds": self.idle_timeout_seconds,
            "idle_seconds": round(idle_seconds, 3),
            "expires_in_seconds": round(
                max(0.0, self.idle_timeout_seconds - idle_seconds), 3
            ),
            "pending_text_uploads": len(self._text_writers),
            "pending_external_imports": self._external_imports,
        }

    def begin_external_import(self, transaction_id):
        transaction = self._require_transaction(transaction_id)
        if self._external_imports >= MAX_EXTERNAL_IMPORTS:
            raise BackendError(
                "Busy",
                "external import concurrency limit reached",
                retryable=True,
                recovery="Wait for an active upload to finish and retry.",
            )
        self._external_imports += 1
        return transaction.id

    def end_external_import(self, transaction_id):
        self._external_imports = max(0, self._external_imports - 1)
        if self._transaction is not None and self._transaction.id == transaction_id:
            self._touch_transaction()

    def transaction_read_text(self, transaction_id, resource_id):
        transaction = self._require_transaction(transaction_id)
        return transaction.read_text(resource_id)

    def transaction_read_text_range(
        self, transaction_id, resource_id, start=0, max_utf16_units=1024 * 1024
    ):
        transaction = self._require_transaction(transaction_id)
        return transaction.read_text_range(resource_id, start, max_utf16_units)

    def transaction_begin_text_write(
        self, transaction_id, resource_id, expected_revision, size
    ):
        transaction = self._require_transaction(transaction_id)
        writer = transaction.begin_text_write(resource_id, size, expected_revision)
        self._text_writers[writer.id] = (transaction.id, writer)
        return {
            "upload_id": writer.id,
            "chunk_size": writer.chunk_size,
            "max_size": writer.max_size,
            "expected_size": writer.expected_size,
        }

    def transaction_begin_text_resource(
        self,
        transaction_id,
        book_path,
        size,
        media_type,
        manifest_id=None,
        properties=None,
        add_to_spine=True,
        manifested=True,
    ):
        transaction = self._require_transaction(transaction_id)
        writer = transaction.begin_text_add(
            book_path,
            size,
            media_type,
            manifest_id=manifest_id,
            properties=properties,
            add_to_spine=add_to_spine,
            manifested=manifested,
        )
        self._text_writers[writer.id] = (transaction.id, writer)
        return {
            "upload_id": writer.id,
            "chunk_size": writer.chunk_size,
            "max_size": writer.max_size,
            "expected_size": writer.expected_size,
        }

    def transaction_write_text_chunk(self, transaction_id, upload_id, offset, text):
        transaction = self._require_transaction(transaction_id)
        writer = self._require_text_writer(transaction.id, upload_id)
        previous = writer.received
        received = writer.write(text, offset=offset)
        return {
            "upload_id": upload_id,
            "received": received,
            "duplicate": offset < previous and received == previous,
        }

    def transaction_finish_text_write(self, transaction_id, upload_id):
        transaction = self._require_transaction(transaction_id)
        writer = self._require_text_writer(transaction.id, upload_id)
        result = writer.finish()
        self._text_writers.pop(upload_id, None)
        return result

    def transaction_abort_text_write(self, transaction_id, upload_id):
        transaction = self._require_transaction(transaction_id)
        writer = self._require_text_writer(transaction.id, upload_id)
        aborted = writer.abort()
        self._text_writers.pop(upload_id, None)
        return {"upload_id": upload_id, "aborted": aborted}

    def transaction_replace_text(
        self, transaction_id, resource_id, expected_revision, text
    ):
        transaction = self._require_transaction(transaction_id)
        return transaction.replace_text(resource_id, text, expected_revision)

    def transaction_apply_edits(
        self, transaction_id, resource_id, expected_revision, edits
    ):
        transaction = self._require_transaction(transaction_id)
        return transaction.apply_edits(resource_id, edits, expected_revision)

    def transaction_add_text_resource(
        self,
        transaction_id,
        book_path,
        text,
        media_type,
        manifest_id=None,
        properties=None,
        add_to_spine=True,
        manifested=True,
    ):
        transaction = self._require_transaction(transaction_id)
        return transaction.add_resource(
            book_path,
            text,
            media_type,
            manifest_id=manifest_id,
            properties=properties,
            add_to_spine=add_to_spine,
            manifested=manifested,
        )

    def transaction_add_binary_resource(
        self,
        transaction_id,
        book_path,
        data_base64,
        media_type,
        manifest_id=None,
        properties=None,
        add_to_spine=False,
        manifested=True,
    ):
        try:
            data = base64.b64decode(data_base64, validate=True)
        except (binascii.Error, ValueError, TypeError) as error:
            raise BackendError(
                "InvalidRequest", "data_base64 must be strict Base64"
            ) from error
        if len(data) > MAX_INLINE_BINARY_SIZE:
            raise BackendError(
                "PayloadTooLarge",
                "decoded binary resource exceeds the 5 MiB inline limit",
                retryable=True,
                recovery="Submit a resource no larger than 5 MiB.",
            )
        transaction = self._require_transaction(transaction_id)
        return transaction.add_resource(
            book_path,
            data,
            media_type,
            manifest_id=manifest_id,
            properties=properties,
            add_to_spine=add_to_spine,
            manifested=manifested,
        )

    def transaction_import_file(
        self,
        transaction_id,
        path,
        kind,
        operation="add",
        book_path=None,
        media_type=None,
        resource_id=None,
        expected_revision=None,
        manifest_id=None,
        properties=None,
        fallback=None,
        overlay=None,
        add_to_spine=False,
        manifested=True,
    ):
        """Stage a verified upload without putting its bytes in an MCP message."""
        if kind not in {"text", "binary"}:
            raise BackendError("InvalidRequest", "kind must be text or binary")
        if operation not in {"add", "replace"}:
            raise BackendError("InvalidRequest", "operation must be add or replace")
        source = pathlib.Path(path)
        try:
            size = source.stat().st_size
        except OSError as error:
            raise BackendError(
                "ResourceNotFound", "external import temporary file is unavailable"
            ) from error
        if size > MAX_EXTERNAL_IMPORT_SIZE:
            raise BackendError(
                "PayloadTooLarge",
                "external import exceeds the 32 MiB limit",
                retryable=True,
                recovery="Split the resource or reduce it below 32 MiB.",
            )

        transaction = self._require_transaction(transaction_id)
        if operation == "replace":
            if not resource_id or expected_revision is None:
                raise BackendError(
                    "InvalidRequest",
                    "replace requires resource_id and expected_revision",
                )
            if kind == "binary":
                result = transaction.write_binary_file(
                    resource_id, str(source), expected_revision
                )
            else:
                result = transaction.replace_text(
                    resource_id, self._read_utf8(source), expected_revision
                )
        else:
            if not book_path or not media_type:
                raise BackendError(
                    "InvalidRequest", "add requires book_path and media_type"
                )
            if kind == "binary" and size > self._external_binary_add_max():
                raise BackendError(
                    "PayloadTooLarge",
                    "binary addition exceeds this Live session message budget",
                    retryable=True,
                    recovery=(
                        "Split the asset or reduce it below {0} bytes. Binary replacement "
                        "and text imports use streaming writers."
                    ).format(self._external_binary_add_max()),
                )
            data = self._read_utf8(source) if kind == "text" else source.read_bytes()
            result = transaction.add_resource(
                book_path,
                data,
                media_type,
                manifest_id=manifest_id,
                properties=properties,
                fallback=fallback,
                overlay=overlay,
                add_to_spine=add_to_spine,
                manifested=manifested,
            )
        value = dict(result)
        value["external_import"] = {
            "operation": operation,
            "kind": kind,
            "size": size,
            "sha256": self._file_sha256(source),
        }
        return value

    def transaction_remove_resource(
        self, transaction_id, resource_id, expected_revision
    ):
        transaction = self._require_transaction(transaction_id)
        return transaction.remove_resource(resource_id, expected_revision)

    def transaction_move_resource(
        self, transaction_id, resource_id, book_path, expected_revision
    ):
        transaction = self._require_transaction(transaction_id)
        return transaction.move_resource(resource_id, book_path, expected_revision)

    def transaction_rename_resource(
        self, transaction_id, resource_id, filename, expected_revision
    ):
        transaction = self._require_transaction(transaction_id)
        return transaction.rename_resource(resource_id, filename, expected_revision)

    def transaction_replace_package(
        self, transaction_id, text, expected_revision
    ):
        transaction = self._require_transaction(transaction_id)
        return transaction.replace_package(text, expected_revision)

    def transaction_update_metadata(
        self, transaction_id, items, expected_revision=None
    ):
        transaction = self._require_transaction(transaction_id)
        return transaction.update_metadata(items, expected_revision)

    def transaction_update_spine(
        self, transaction_id, items, attributes=None, expected_revision=None
    ):
        transaction = self._require_transaction(transaction_id)
        return transaction.update_spine(items, attributes, expected_revision)

    def transaction_preview(self, transaction_id):
        transaction = self._require_transaction(transaction_id)
        return transaction.preview()

    def transaction_validate(self, transaction_id):
        transaction = self._require_transaction(transaction_id)
        return transaction.validate()

    def transaction_commit(self, transaction_id):
        transaction = self._require_transaction(transaction_id)
        if self._external_imports:
            raise BackendError(
                "Busy",
                "cannot commit while an external import is uploading",
                retryable=True,
                recovery="Wait for the uploader to finish, then preview and validate again.",
            )
        result = transaction.commit()
        self._text_writers.clear()
        self._transaction = None
        self._transaction_activity = None
        result = dict(result)
        result["confirmation_required"] = False
        return result

    def transaction_rollback(self, transaction_id):
        transaction = self._require_transaction(transaction_id)
        result = transaction.rollback()
        self._text_writers.clear()
        self._transaction = None
        self._transaction_activity = None
        self._external_imports = 0
        return result

    def expire_idle_transaction(self):
        if self._transaction is None or self._transaction_activity is None:
            return False
        if self._external_imports:
            return False
        if time.monotonic() - self._transaction_activity < self.idle_timeout_seconds:
            return False
        self._transaction.rollback()
        self._text_writers.clear()
        self._transaction = None
        self._transaction_activity = None
        self._external_imports = 0
        return True

    def shutdown(self):
        if self._transaction is None:
            return False
        try:
            self._transaction.rollback()
        finally:
            self._text_writers.clear()
            self._transaction = None
            self._transaction_activity = None
            self._external_imports = 0
        return True

    def _require_transaction(self, transaction_id):
        if self._transaction is None:
            raise BackendError(
                "TransactionNotFound",
                "There is no active MCP transaction for this Book",
                retryable=True,
                recovery="Begin a transaction and restage the changes.",
            )
        if transaction_id is not None and transaction_id != self._transaction.id:
            raise BackendError(
                "TransactionNotFound",
                "The transaction ID is stale or belongs to another Book session",
                retryable=True,
                recovery="Use the transaction ID returned by this endpoint.",
            )
        self._touch_transaction()
        return self._transaction

    def _touch_transaction(self):
        self._transaction_activity = time.monotonic()

    def _require_text_writer(self, transaction_id, upload_id):
        owner = self._text_writers.get(upload_id)
        if owner is None or owner[0] != transaction_id:
            raise BackendError(
                "ResourceNotFound",
                "Text write upload not found",
                retryable=True,
                recovery="Begin a new text write upload.",
            )
        return owner[1]

    @staticmethod
    def _read_utf8(path):
        try:
            return path.read_text(encoding="utf-8")
        except UnicodeDecodeError as error:
            raise BackendError(
                "ValidationFailed",
                "text external import is not strict UTF-8",
                recovery="Convert the source file to UTF-8 or upload it as binary.",
            ) from error

    @staticmethod
    def _file_sha256(path):
        digest = hashlib.sha256()
        with path.open("rb") as stream:
            for chunk in iter(lambda: stream.read(1024 * 1024), b""):
                digest.update(chunk)
        return digest.hexdigest()

    def _external_binary_add_max(self):
        maximum = int(self.plugin.session_info.get("max_message_size") or 0)
        payload_budget = max(0, maximum - 1024 * 1024)
        return min(MAX_EXTERNAL_IMPORT_SIZE, payload_budget * 3 // 4)
