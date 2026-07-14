import base64
import platform
from dataclasses import dataclass

from .rpc import RpcClient
from .transport import LocalSocketTransport


@dataclass(frozen=True)
class Resource:
    id: str
    book_path: str
    media_type: str
    resource_type: str
    revision: int
    loaded: bool

    @classmethod
    def from_result(cls, value):
        return cls(
            id=value["resource_id"],
            book_path=value["book_path"],
            media_type=value.get("media_type", ""),
            resource_type=value.get("resource_type", "binary"),
            revision=value.get("content_revision", 1),
            loaded=value.get("loaded", True),
        )


@dataclass(frozen=True)
class Selection:
    start: int
    end: int
    text: str


@dataclass(frozen=True)
class EditorState:
    active: bool
    resource_id: str = None
    book_path: str = None
    revision: int = None
    cursor: int = None
    selection: Selection = None
    position_encoding: str = "utf-16"

    @classmethod
    def from_result(cls, value):
        selection = value.get("selection")
        return cls(
            active=value.get("active", False),
            resource_id=value.get("resource_id"),
            book_path=value.get("book_path"),
            revision=value.get("revision"),
            cursor=value.get("cursor"),
            selection=Selection(**selection) if selection is not None else None,
            position_encoding=value.get("position_encoding", "utf-16"),
        )


class Transaction:
    def __init__(self, rpc, result):
        self._rpc = rpc
        self.id = result["transaction_id"]
        self.base_book_revision = result["base_book_revision"]
        self.checkpoint = result["checkpoint"]
        self.active = True

    def _params(self, values=None):
        if not self.active:
            raise RuntimeError("transaction is no longer active")
        params = {"transaction_id": self.id}
        if values:
            params.update(values)
        return params

    def read_text(self, resource):
        resource_id = resource.id if isinstance(resource, Resource) else resource
        return self._rpc.call(
            "transaction.readText", self._params({"resource_id": resource_id})
        )

    def replace_text(self, resource, text, expected_revision=None):
        resource_id = resource.id if isinstance(resource, Resource) else resource
        if expected_revision is None:
            expected_revision = self.read_text(resource_id)["revision"]
        return self._rpc.call(
            "transaction.replaceText",
            self._params(
                {
                    "resource_id": resource_id,
                    "expected_revision": expected_revision,
                    "text": text,
                }
            ),
        )

    def apply_edits(self, resource, edits, expected_revision=None):
        resource_id = resource.id if isinstance(resource, Resource) else resource
        if expected_revision is None:
            expected_revision = self.read_text(resource_id)["revision"]
        normalized = []
        for edit in edits:
            if isinstance(edit, dict):
                normalized.append({"start": edit["start"], "end": edit["end"], "text": edit["text"]})
            else:
                start, end, text = edit
                normalized.append({"start": start, "end": end, "text": text})
        return self._rpc.call(
            "transaction.applyTextEdits",
            self._params(
                {
                    "resource_id": resource_id,
                    "expected_revision": expected_revision,
                    "edits": normalized,
                }
            ),
        )

    def read_binary(self, resource):
        resource_id = resource.id if isinstance(resource, Resource) else resource
        result = self._rpc.call(
            "transaction.readBinary", self._params({"resource_id": resource_id})
        )
        result["data"] = base64.b64decode(result.pop("data_base64"), validate=True)
        return result

    def write_binary(self, resource, data, expected_revision=None):
        resource_id = resource.id if isinstance(resource, Resource) else resource
        if expected_revision is None:
            expected_revision = self.read_binary(resource_id)["revision"]
        if not isinstance(data, (bytes, bytearray, memoryview)):
            raise TypeError("binary data must be bytes-like")
        return self._rpc.call(
            "transaction.writeBinary",
            self._params(
                {
                    "resource_id": resource_id,
                    "expected_revision": expected_revision,
                    "data_base64": base64.b64encode(bytes(data)).decode("ascii"),
                }
            ),
        )

    def replace_package(self, text, expected_revision):
        """Stage an authoritative OPF package document replacement."""
        return self._rpc.call(
            "transaction.replacePackage",
            self._params(
                {"expected_revision": expected_revision, "text": text}
            ),
        )

    def replace_archive_file(self, book_path, data, expected_sha256):
        params = {
            "book_path": book_path,
            "expected_sha256": expected_sha256,
        }
        if isinstance(data, str):
            params["text"] = data
        elif isinstance(data, (bytes, bytearray, memoryview)):
            params["data_base64"] = base64.b64encode(bytes(data)).decode("ascii")
        else:
            raise TypeError("archive file data must be str or bytes-like")
        return self._rpc.call(
            "transaction.replaceArchiveFile", self._params(params)
        )

    def remove_archive_file(self, book_path, expected_sha256):
        return self._rpc.call(
            "transaction.removeArchiveFile",
            self._params(
                {"book_path": book_path, "expected_sha256": expected_sha256}
            ),
        )

    def add_resource(
        self,
        book_path,
        data,
        media_type,
        manifest_id=None,
        properties=None,
        fallback=None,
        overlay=None,
        add_to_spine=True,
        manifested=True,
    ):
        params = {
            "book_path": book_path,
            "media_type": media_type,
            "manifest_id": manifest_id,
            "properties": properties,
            "fallback": fallback,
            "overlay": overlay,
            "add_to_spine": add_to_spine,
            "manifested": manifested,
        }
        if isinstance(data, str):
            params["text"] = data
        elif isinstance(data, (bytes, bytearray, memoryview)):
            params["data_base64"] = base64.b64encode(bytes(data)).decode("ascii")
        else:
            raise TypeError("resource data must be str or bytes-like")
        return self._rpc.call("transaction.addResource", self._params(params))

    def remove_resource(self, resource, expected_revision=None):
        resource_id = resource.id if isinstance(resource, Resource) else resource
        if expected_revision is None:
            if not isinstance(resource, Resource):
                raise ValueError("expected_revision is required when resource is an ID")
            expected_revision = resource.revision
        return self._rpc.call(
            "transaction.removeResource",
            self._params(
                {"resource_id": resource_id, "expected_revision": expected_revision}
            ),
        )

    def move_resource(self, resource, book_path, expected_revision=None):
        resource_id = resource.id if isinstance(resource, Resource) else resource
        if expected_revision is None:
            if not isinstance(resource, Resource):
                raise ValueError("expected_revision is required when resource is an ID")
            expected_revision = resource.revision
        return self._rpc.call(
            "transaction.moveResource",
            self._params(
                {
                    "resource_id": resource_id,
                    "book_path": book_path,
                    "expected_revision": expected_revision,
                }
            ),
        )

    def rename_resource(self, resource, filename, expected_revision=None):
        resource_id = resource.id if isinstance(resource, Resource) else resource
        if expected_revision is None:
            if not isinstance(resource, Resource):
                raise ValueError("expected_revision is required when resource is an ID")
            expected_revision = resource.revision
        return self._rpc.call(
            "transaction.renameResource",
            self._params(
                {
                    "resource_id": resource_id,
                    "filename": filename,
                    "expected_revision": expected_revision,
                }
            ),
        )

    def validate(self):
        return self._rpc.call("transaction.validate", self._params())

    def preview(self):
        return self._rpc.call("transaction.preview", self._params())

    def commit(self):
        result = self._rpc.call("transaction.commit", self._params())
        self.active = False
        return result

    def rollback(self):
        if not self.active:
            return None
        result = self._rpc.call("transaction.rollback", self._params())
        self.active = False
        return result

    def __enter__(self):
        return self

    def __exit__(self, exc_type, exc_value, traceback):
        if self.active:
            self.rollback()
        return False


class BookApi:
    def __init__(self, rpc):
        self._rpc = rpc

    def get_info(self):
        return self._rpc.call("book.getInfo")

    def get_compatibility_snapshot(self):
        """Return the immutable startup state used by the legacy API adapter."""
        return self._rpc.call("book.getCompatibilitySnapshot")

    def get_revision(self):
        return self._rpc.call("book.getRevision")["revision"]

    def archive_files(self, page_size=200):
        cursor = None
        while True:
            params = {"page_size": page_size}
            if cursor is not None:
                params["cursor"] = cursor
            result = self._rpc.call("archive.listFiles", params)
            yield from result["items"]
            cursor = result.get("next_cursor")
            if cursor is None:
                break

    def read_archive_file(self, book_path):
        result = self._rpc.call("archive.readFile", {"book_path": book_path})
        result["data"] = base64.b64decode(result.pop("data_base64"), validate=True)
        return result

    def resources(self, types=None, page_size=200):
        cursor = None
        while True:
            params = {"page_size": page_size}
            if types:
                params["types"] = list(types)
            if cursor is not None:
                params["cursor"] = cursor
            result = self._rpc.call("resource.list", params)
            for item in result["items"]:
                yield Resource.from_result(item)
            cursor = result.get("next_cursor")
            if cursor is None:
                break

    def text_resources(self):
        return self.resources(types=("html", "css", "xml", "text", "opf", "ncx"))

    def resolve_path(self, book_path):
        return Resource.from_result(self._rpc.call("resource.resolvePath", {"book_path": book_path}))

    def get_resource(self, resource_id):
        return Resource.from_result(self._rpc.call("resource.getInfo", {"resource_id": resource_id}))

    def read_text(self, resource):
        resource_id = resource.id if isinstance(resource, Resource) else resource
        return self._rpc.call("resource.readText", {"resource_id": resource_id})

    def read_many(self, resources):
        ids = [item.id if isinstance(item, Resource) else item for item in resources]
        return self._rpc.call("resource.readMany", {"resource_ids": ids})["items"]

    def read_binary(self, resource):
        resource_id = resource.id if isinstance(resource, Resource) else resource
        result = self._rpc.call("resource.readBinary", {"resource_id": resource_id})
        result["data"] = base64.b64decode(result.pop("data_base64"), validate=True)
        return result

    def transaction(self, label="Plugin changes", checkpoint="auto"):
        result = self._rpc.call(
            "transaction.begin",
            {"label": label, "visibility": "staged", "checkpoint": checkpoint},
        )
        return Transaction(self._rpc, result)


class EditorApi:
    def __init__(self, rpc):
        self._rpc = rpc

    def get_state(self):
        return EditorState.from_result(self._rpc.call("editor.getState"))

    def get_selection(self):
        return EditorState.from_result(self._rpc.call("editor.getSelection")).selection

    def get_open_tabs(self):
        return [Resource.from_result(item) for item in self._rpc.call("editor.getOpenTabs")["items"]]

    def apply_edits(self, edits, expected_revision=None, resource_id=None, label="Plugin edit"):
        state = self.get_state() if expected_revision is None or resource_id is None else None
        if expected_revision is None:
            expected_revision = state.revision
        if resource_id is None:
            resource_id = state.resource_id
        normalized = []
        for edit in edits:
            if isinstance(edit, dict):
                normalized.append({"start": edit["start"], "end": edit["end"], "text": edit["text"]})
            else:
                start, end, text = edit
                normalized.append({"start": start, "end": end, "text": text})
        return self._rpc.call(
            "editor.applyEdits",
            {
                "resource_id": resource_id,
                "expected_revision": expected_revision,
                "label": label,
                "edits": normalized,
            },
        )

    def replace_selection(self, text, expected_revision=None, resource_id=None, label="Replace selection"):
        state = self.get_state() if expected_revision is None or resource_id is None else None
        return self._rpc.call(
            "editor.replaceSelection",
            {
                "resource_id": resource_id if resource_id is not None else state.resource_id,
                "expected_revision": expected_revision if expected_revision is not None else state.revision,
                "label": label,
                "text": text,
            },
        )

    def insert_text(self, text, expected_revision=None, resource_id=None, label="Insert text"):
        state = self.get_state() if expected_revision is None or resource_id is None else None
        return self._rpc.call(
            "editor.insertText",
            {
                "resource_id": resource_id if resource_id is not None else state.resource_id,
                "expected_revision": expected_revision if expected_revision is not None else state.revision,
                "label": label,
                "text": text,
            },
        )

    def set_cursor(self, position, resource_id=None):
        params = {"position": position}
        if resource_id is not None:
            params["resource_id"] = resource_id
        return EditorState.from_result(self._rpc.call("editor.setCursor", params))

    def set_selection(self, start, end, resource_id=None):
        params = {"start": start, "end": end}
        if resource_id is not None:
            params["resource_id"] = resource_id
        return EditorState.from_result(self._rpc.call("editor.setSelection", params))

    def open_resource(self, resource, position=None):
        resource_id = resource.id if isinstance(resource, Resource) else resource
        params = {"resource_id": resource_id}
        if position is not None:
            params["position"] = position
        return EditorState.from_result(self._rpc.call("editor.openResource", params))

    def reveal_range(self, resource, start, end):
        resource_id = resource.id if isinstance(resource, Resource) else resource
        return EditorState.from_result(
            self._rpc.call(
                "editor.revealRange",
                {"resource_id": resource_id, "start": start, "end": end},
            )
        )


class ValidationApi:
    def __init__(self, rpc):
        self._rpc = rpc

    def publish_results(self, results):
        normalized = []
        for result in results:
            if isinstance(result, dict):
                normalized.append(
                    {
                        "type": result["type"],
                        "book_path": result.get("book_path", ""),
                        "line": result.get("line", -1),
                        "character": result.get("character", -1),
                        "message": result["message"],
                    }
                )
            else:
                normalized.append(
                    {
                        "type": result.restype,
                        "book_path": result.bookpath,
                        "line": int(result.linenumber),
                        "character": int(result.charoffset),
                        "message": result.message,
                    }
                )
        return self._rpc.call("validation.publishResults", {"results": normalized})


class UiApi:
    def __init__(self, rpc):
        self._rpc = rpc

    def show_status(self, message, duration_ms=5000):
        return self._rpc.call(
            "ui.showStatus", {"message": message, "duration_ms": duration_ms}
        )["shown"]

    def show_message(self, message, title=None, level="info"):
        params = {"message": message, "level": level}
        if title is not None:
            params["title"] = title
        return self._rpc.call("ui.showMessage", params)["shown"]

    def confirm(self, message, title=None):
        params = {"message": message}
        if title is not None:
            params["title"] = title
        return self._rpc.call("ui.confirm", params)["confirmed"]


class EventsApi:
    def __init__(self, rpc):
        self._rpc = rpc

    def subscribe(self, *events):
        return self._rpc.call("events.subscribe", {"events": list(events)})["subscribed"]

    def unsubscribe(self, *events):
        return self._rpc.call("events.unsubscribe", {"events": list(events)})["subscribed"]

    @staticmethod
    def _event(notification):
        if notification is None:
            return None
        return {"name": notification["method"], "params": notification.get("params", {})}

    def poll(self):
        return self._event(self._rpc.poll_notification())

    def next_event(self):
        """Wait for the next event; do not consume one RPC connection concurrently."""
        return self._event(self._rpc.next_notification())


class Plugin:
    def __init__(self, rpc, session_info, transport):
        self._rpc = rpc
        self._transport = transport
        self.session_info = session_info
        self.book = BookApi(rpc)
        self.editor = EditorApi(rpc)
        self.validation = ValidationApi(rpc)
        self.ui = UiApi(rpc)
        self.events = EventsApi(rpc)

    @classmethod
    def connect(cls, socket_name, token, plugin_name):
        transport = LocalSocketTransport(socket_name)
        transport.connect()
        rpc = RpcClient(transport)
        session_info = rpc.call(
            "session.hello",
            {
                "token": token,
                "protocol_version": 1,
                "api_version": 2,
                "plugin_name": plugin_name,
                "client": {"python": platform.python_version(), "library": "sigil_live/2.0.0"},
                "capabilities": {"events": True, "binary_chunks": False, "position_encodings": ["utf-16"]},
            },
        )
        transport.max_message_size = session_info["max_message_size"]
        return cls(rpc, session_info, transport)

    def ping(self):
        return self._rpc.call("session.ping")["pong"]

    def finish(self, status="success", message=""):
        return self._rpc.call("session.finish", {"status": status, "message": message})

    def close(self):
        self._transport.close()
