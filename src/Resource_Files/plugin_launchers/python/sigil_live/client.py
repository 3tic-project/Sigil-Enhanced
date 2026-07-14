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


class BookApi:
    def __init__(self, rpc):
        self._rpc = rpc

    def get_info(self):
        return self._rpc.call("book.getInfo")

    def get_revision(self):
        return self._rpc.call("book.getRevision")["revision"]

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


class EditorApi:
    def __init__(self, rpc):
        self._rpc = rpc

    def get_state(self):
        return EditorState.from_result(self._rpc.call("editor.getState"))

    def get_selection(self):
        return EditorState.from_result(self._rpc.call("editor.getSelection")).selection

    def get_open_tabs(self):
        return [Resource.from_result(item) for item in self._rpc.call("editor.getOpenTabs")["items"]]


class Plugin:
    def __init__(self, rpc, session_info, transport):
        self._rpc = rpc
        self._transport = transport
        self.session_info = session_info
        self.book = BookApi(rpc)
        self.editor = EditorApi(rpc)

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
                "capabilities": {"events": False, "binary_chunks": False, "position_encodings": ["utf-16"]},
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
