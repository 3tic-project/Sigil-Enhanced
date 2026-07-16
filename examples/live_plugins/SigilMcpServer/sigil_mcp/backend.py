import base64
import binascii
import dataclasses
import time

from .catalog import PROMPT_NAMES, RESOURCE_URIS, TOOL_NAMES
from .errors import BackendError
from . import MCP_PROTOCOL_VERSION, SERVER_NAME, SERVER_VERSION


MAX_INLINE_BINARY_SIZE = 5 * 1024 * 1024


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
        return {
            "tools": list(TOOL_NAMES),
            "resources": list(RESOURCE_URIS),
            "prompts": list(PROMPT_NAMES),
            "limits": {
                "resource_page_size_max": 500,
                "read_many_max": 100,
                "inline_binary_size_max": MAX_INLINE_BINARY_SIZE,
                "editor_edits_max": 1000,
                "position_encoding": "utf-16",
                "active_transactions": 1,
                "transaction_idle_timeout_seconds": self.idle_timeout_seconds,
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

    def resource_read_many(self, resource_ids):
        if not 1 <= len(resource_ids) <= 100:
            raise BackendError(
                "InvalidRequest", "resource_ids must contain between 1 and 100 IDs"
            )
        return {"items": self.plugin.book.read_many(resource_ids)}

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
        self, resource_id, expected_revision, text, label="MCP replace selection"
    ):
        return self.plugin.editor.replace_selection(
            text,
            expected_revision=expected_revision,
            resource_id=resource_id,
            label=label,
        )

    def editor_insert_text(
        self, resource_id, expected_revision, text, label="MCP insert text"
    ):
        return self.plugin.editor.insert_text(
            text,
            expected_revision=expected_revision,
            resource_id=resource_id,
            label=label,
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
        transaction = self._require_transaction(None)
        return {
            "transaction_id": transaction.id,
            "base_book_revision": transaction.base_book_revision,
            "checkpoint": transaction.checkpoint,
            "idle_timeout_seconds": self.idle_timeout_seconds,
        }

    def transaction_read_text(self, transaction_id, resource_id):
        transaction = self._require_transaction(transaction_id)
        return transaction.read_text(resource_id)

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
        result = transaction.commit()
        self._transaction = None
        self._transaction_activity = None
        result = dict(result)
        result["confirmation_required"] = False
        return result

    def transaction_rollback(self, transaction_id):
        transaction = self._require_transaction(transaction_id)
        result = transaction.rollback()
        self._transaction = None
        self._transaction_activity = None
        return result

    def expire_idle_transaction(self):
        if self._transaction is None or self._transaction_activity is None:
            return False
        if time.monotonic() - self._transaction_activity < self.idle_timeout_seconds:
            return False
        self._transaction.rollback()
        self._transaction = None
        self._transaction_activity = None
        return True

    def shutdown(self):
        if self._transaction is None:
            return False
        try:
            self._transaction.rollback()
        finally:
            self._transaction = None
            self._transaction_activity = None
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
