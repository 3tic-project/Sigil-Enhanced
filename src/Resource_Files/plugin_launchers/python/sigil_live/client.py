import base64
import hashlib
import os
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


class BinaryReader:
    """A bounded-chunk reader for a session-owned binary snapshot."""

    def __init__(self, rpc, result):
        self._rpc = rpc
        self.id = result["stream_id"]
        self.resource_id = result.get("resource_id")
        self.book_path = result["book_path"]
        self.revision = result.get("revision")
        self.size = result["size"]
        self.sha256 = result["sha256"]
        self.chunk_size = result["chunk_size"]
        self.closed = False

    def chunks(self, max_bytes=None):
        if self.closed:
            raise RuntimeError("binary stream is closed")
        size = self.chunk_size if max_bytes is None else max_bytes
        while True:
            result = self._rpc.call(
                "binary.readChunk", {"stream_id": self.id, "max_bytes": size}
            )
            data = base64.b64decode(result["data_base64"], validate=True)
            if data:
                yield data
            if result["eof"]:
                break

    def read(self, max_bytes=None):
        return b"".join(self.chunks(max_bytes))

    def close(self):
        if not self.closed:
            self._rpc.call("binary.close", {"stream_id": self.id})
            self.closed = True

    def __enter__(self):
        return self

    def __exit__(self, exc_type, exc_value, traceback):
        self.close()
        return False


class _BinaryWriter:
    def __init__(self, transaction, resource_id, size, expected_revision):
        self._transaction = transaction
        self._rpc = transaction._rpc
        result = self._rpc.call(
            "transaction.writeBinaryBegin",
            transaction._params(
                {
                    "resource_id": resource_id,
                    "size": size,
                    "expected_revision": expected_revision,
                }
            ),
        )
        self.id = result["upload_id"]
        self.chunk_size = result["chunk_size"]
        self.received = 0
        self._hash = hashlib.sha256()
        self.finished = False

    def write(self, data):
        if self.finished:
            raise RuntimeError("binary write is finished")
        if not isinstance(data, (bytes, bytearray, memoryview)):
            raise TypeError("binary data must be bytes-like")
        value = memoryview(data)
        for offset in range(0, len(value), self.chunk_size):
            chunk = bytes(value[offset:offset + self.chunk_size])
            if not chunk:
                continue
            result = self._rpc.call(
                "transaction.writeBinaryChunk",
                self._transaction._params(
                    {
                        "upload_id": self.id,
                        "data_base64": base64.b64encode(chunk).decode("ascii"),
                    }
                ),
            )
            self._hash.update(chunk)
            self.received = result["received"]
        return self.received

    def finish(self):
        if self.finished:
            raise RuntimeError("binary write is finished")
        result = self._rpc.call(
            "transaction.writeBinaryEnd",
            self._transaction._params(
                {"upload_id": self.id, "sha256": self._hash.hexdigest()}
            ),
        )
        self.finished = True
        return result


def _text_chunks(value, maximum_bytes):
    start = 0
    while start < len(value):
        end = min(len(value), start + maximum_bytes)
        chunk = value[start:end]
        encoded = chunk.encode("utf-8")
        while len(encoded) > maximum_bytes:
            width = max(1, int((end - start) * maximum_bytes / len(encoded)))
            end = start + width
            chunk = value[start:end]
            encoded = chunk.encode("utf-8")
        yield chunk, encoded
        start = end


class _TextWriter:
    """An integrity-checked chunked transaction writer for UTF-8 text."""

    def __init__(self, transaction, method, params):
        self._transaction = transaction
        self._rpc = transaction._rpc
        result = self._rpc.call(method, transaction._params(params))
        self.id = result["upload_id"]
        self.chunk_size = result["chunk_size"]
        self.max_size = result["max_size"]
        self.expected_size = result["expected_size"]
        self.received = 0
        self._hash = hashlib.sha256()
        self.finished = False

    def write(self, text, offset=None):
        if self.finished:
            raise RuntimeError("text write is finished")
        if not isinstance(text, str):
            raise TypeError("text must be a string")
        cursor = self.received if offset is None else offset
        for chunk, encoded in _text_chunks(text, self.chunk_size):
            result = self._rpc.call(
                "transaction.writeTextChunk",
                self._transaction._params(
                    {"upload_id": self.id, "offset": cursor, "text": chunk}
                ),
            )
            if not result.get("duplicate", False):
                self._hash.update(encoded)
            self.received = result["received"]
            cursor = self.received
        return self.received

    def finish(self):
        if self.finished:
            raise RuntimeError("text write is finished")
        result = self._rpc.call(
            "transaction.writeTextEnd",
            self._transaction._params(
                {"upload_id": self.id, "sha256": self._hash.hexdigest()}
            ),
        )
        self.finished = True
        return result

    def abort(self):
        if self.finished:
            return False
        result = self._rpc.call(
            "transaction.writeTextAbort",
            self._transaction._params({"upload_id": self.id}),
        )
        self.finished = True
        return result["aborted"]


class InputWriter:
    """Chunked, integrity-checked EPUB upload owned by an input plugin."""

    def __init__(self, rpc, filename, size=None):
        self._rpc = rpc
        params = {"filename": filename}
        if size is not None:
            params["size"] = size
        result = rpc.call("input.beginEpub", params)
        self.id = result["upload_id"]
        self.chunk_size = result["chunk_size"]
        self.max_size = result["max_size"]
        self.received = 0
        self._hash = hashlib.sha256()
        self.finished = False

    def write(self, data):
        if self.finished:
            raise RuntimeError("input upload is finished")
        if not isinstance(data, (bytes, bytearray, memoryview)):
            raise TypeError("EPUB data must be bytes-like")
        value = memoryview(data)
        for offset in range(0, len(value), self.chunk_size):
            chunk = bytes(value[offset:offset + self.chunk_size])
            if not chunk:
                continue
            result = self._rpc.call(
                "input.writeChunk",
                {
                    "upload_id": self.id,
                    "data_base64": base64.b64encode(chunk).decode("ascii"),
                },
            )
            self._hash.update(chunk)
            self.received = result["received"]
        return self.received

    def finish(self):
        if self.finished:
            raise RuntimeError("input upload is finished")
        result = self._rpc.call(
            "input.finishEpub",
            {"upload_id": self.id, "sha256": self._hash.hexdigest()},
        )
        self.finished = True
        return result


class InputApi:
    def __init__(self, rpc):
        self._rpc = rpc

    def begin_epub(self, filename, size=None):
        return InputWriter(self._rpc, filename, size)

    def submit_epub(self, data, filename="input.epub"):
        writer = self.begin_epub(filename, len(data))
        writer.write(data)
        return writer.finish()

    def submit_epub_file(self, path):
        size = os.path.getsize(path)
        writer = self.begin_epub(os.path.basename(path), size)
        with open(path, "rb") as stream:
            while True:
                chunk = stream.read(writer.chunk_size)
                if not chunk:
                    break
                writer.write(chunk)
        return writer.finish()


class OutputApi:
    def __init__(self, rpc):
        self._rpc = rpc

    def export_epub(self, path):
        return self._rpc.call("output.exportEpub", {"path": os.path.abspath(path)})

    def save_source(self):
        """Save the current Book back to its existing source EPUB."""
        return self._rpc.call("output.exportEpub", {})


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

    def read_text_range(self, resource, start=0, max_utf16_units=1024 * 1024):
        resource_id = resource.id if isinstance(resource, Resource) else resource
        return self._rpc.call(
            "transaction.readTextRange",
            self._params(
                {
                    "resource_id": resource_id,
                    "start": start,
                    "max_utf16_units": max_utf16_units,
                }
            ),
        )

    def replace_text(self, resource, text, expected_revision=None):
        resource_id = resource.id if isinstance(resource, Resource) else resource
        if expected_revision is None:
            expected_revision = self.read_text(resource_id)["revision"]
        encoded_size = len(text.encode("utf-8"))
        if encoded_size > 4 * 1024 * 1024:
            writer = self.begin_text_write(resource_id, encoded_size, expected_revision)
            writer.write(text)
            return writer.finish()
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

    def begin_text_write(self, resource, size, expected_revision=None):
        resource_id = resource.id if isinstance(resource, Resource) else resource
        if expected_revision is None:
            if isinstance(resource, Resource):
                expected_revision = resource.revision
            else:
                expected_revision = self.read_text(resource_id)["revision"]
        return _TextWriter(
            self,
            "transaction.writeTextBegin",
            {
                "resource_id": resource_id,
                "size": size,
                "expected_revision": expected_revision,
            },
        )

    def begin_text_add(
        self,
        book_path,
        size,
        media_type,
        manifest_id=None,
        properties=None,
        fallback=None,
        overlay=None,
        add_to_spine=True,
        manifested=True,
    ):
        return _TextWriter(
            self,
            "transaction.addTextBegin",
            {
                "book_path": book_path,
                "size": size,
                "media_type": media_type,
                "manifest_id": manifest_id,
                "properties": properties,
                "fallback": fallback,
                "overlay": overlay,
                "add_to_spine": add_to_spine,
                "manifested": manifested,
            },
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
        if not isinstance(data, (bytes, bytearray, memoryview)):
            raise TypeError("binary data must be bytes-like")
        if expected_revision is None:
            expected_revision = (
                resource.revision
                if isinstance(resource, Resource)
                else self.read_binary(resource_id)["revision"]
            )
        if len(data) > 4 * 1024 * 1024:
            writer = self.begin_binary_write(resource_id, len(data), expected_revision)
            writer.write(data)
            return writer.finish()
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

    def begin_binary_write(self, resource, size, expected_revision=None):
        resource_id = resource.id if isinstance(resource, Resource) else resource
        if expected_revision is None:
            if not isinstance(resource, Resource):
                raise ValueError("expected_revision is required when resource is an ID")
            expected_revision = resource.revision
        return _BinaryWriter(self, resource_id, size, expected_revision)

    def write_binary_file(self, resource, path, expected_revision=None):
        writer = self.begin_binary_write(
            resource, os.path.getsize(path), expected_revision
        )
        with open(path, "rb") as stream:
            while True:
                chunk = stream.read(writer.chunk_size)
                if not chunk:
                    break
                writer.write(chunk)
        return writer.finish()

    def replace_package(self, text, expected_revision):
        """Stage an authoritative OPF package document replacement."""
        return self._rpc.call(
            "transaction.replacePackage",
            self._params(
                {"expected_revision": expected_revision, "text": text}
            ),
        )

    def update_metadata(self, items, expected_revision=None):
        if expected_revision is None:
            expected_revision = self._rpc.call("book.getMetadata")["revision"]
        return self._rpc.call(
            "transaction.updateMetadata",
            self._params({"items": list(items), "expected_revision": expected_revision}),
        )

    def update_spine(self, items, attributes=None, expected_revision=None):
        if expected_revision is None:
            expected_revision = self._rpc.call("book.getSpine")["revision"]
        return self._rpc.call(
            "transaction.updateSpine",
            self._params(
                {
                    "items": list(items),
                    "attributes": dict(attributes or {}),
                    "expected_revision": expected_revision,
                }
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
        if isinstance(data, str) and len(data.encode("utf-8")) > 4 * 1024 * 1024:
            writer = self.begin_text_add(
                book_path,
                len(data.encode("utf-8")),
                media_type,
                manifest_id=manifest_id,
                properties=properties,
                fallback=fallback,
                overlay=overlay,
                add_to_spine=add_to_spine,
                manifested=manifested,
            )
            writer.write(data)
            return writer.finish()
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

    def get_metadata(self):
        return self._rpc.call("book.getMetadata")

    def get_manifest(self):
        return self._rpc.call("book.getManifest")["items"]

    def get_spine(self):
        return self._rpc.call("book.getSpine")

    def get_guide(self):
        return self._rpc.call("book.getGuide")["items"]

    def get_bindings(self):
        return self._rpc.call("book.getBindings")["items"]

    def get_selection(self):
        return [
            Resource.from_result(item)
            for item in self._rpc.call("book.getSelection")["items"]
        ]

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
            result = self.list_resources(types=types, page_size=page_size, cursor=cursor)
            yield from result["items"]
            cursor = result.get("next_cursor")
            if cursor is None:
                break

    def list_resources(self, types=None, page_size=200, cursor=None):
        """Return one bounded resource page and its opaque continuation cursor."""
        params = {"page_size": page_size}
        if types:
            params["types"] = list(types)
        if cursor is not None:
            params["cursor"] = cursor
        result = self._rpc.call("resource.list", params)
        return {
            "items": [Resource.from_result(item) for item in result["items"]],
            "next_cursor": result.get("next_cursor"),
        }

    def text_resources(self):
        return self.resources(types=("html", "css", "xml", "text", "opf", "ncx"))

    def resolve_path(self, book_path):
        return Resource.from_result(self._rpc.call("resource.resolvePath", {"book_path": book_path}))

    def get_resource(self, resource_id):
        return Resource.from_result(self._rpc.call("resource.getInfo", {"resource_id": resource_id}))

    def read_text(self, resource):
        resource_id = resource.id if isinstance(resource, Resource) else resource
        return self._rpc.call("resource.readText", {"resource_id": resource_id})

    def read_text_range(self, resource, start=0, max_utf16_units=1024 * 1024):
        resource_id = resource.id if isinstance(resource, Resource) else resource
        return self._rpc.call(
            "resource.readTextRange",
            {
                "resource_id": resource_id,
                "start": start,
                "max_utf16_units": max_utf16_units,
            },
        )

    def read_many(self, resources):
        ids = [item.id if isinstance(item, Resource) else item for item in resources]
        items = []
        cursor = None
        while True:
            params = {"resource_ids": ids}
            if cursor is not None:
                params["cursor"] = cursor
            result = self._rpc.call("resource.readMany", params)
            items.extend(result["items"])
            cursor = result.get("next_cursor")
            if cursor is None:
                return items

    def read_binary(self, resource):
        resource_id = resource.id if isinstance(resource, Resource) else resource
        result = self._rpc.call("resource.readBinary", {"resource_id": resource_id})
        result["data"] = base64.b64decode(result.pop("data_base64"), validate=True)
        return result

    def open_binary(self, resource):
        """Open a chunked snapshot reader without the inline payload limit."""
        resource_id = resource.id if isinstance(resource, Resource) else resource
        return BinaryReader(
            self._rpc,
            self._rpc.call("binary.openRead", {"resource_id": resource_id}),
        )

    def open_archive_file(self, book_path):
        """Open any expanded EPUB file as a chunked stable snapshot."""
        return BinaryReader(
            self._rpc,
            self._rpc.call("binary.openRead", {"book_path": book_path}),
        )

    def materialize_temporary(self, resource=None, book_path=None):
        if (resource is None) == (book_path is None):
            raise ValueError("provide exactly one of resource or book_path")
        params = {}
        if resource is not None:
            params["resource_id"] = resource.id if isinstance(resource, Resource) else resource
        else:
            params["book_path"] = book_path
        return self._rpc.call("resource.materializeTemporary", params)

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


class Progress:
    def __init__(self, rpc, result):
        self._rpc = rpc
        self.id = result["progress_id"]
        self.total = result["total"]
        self.ended = False

    def update(self, value, label=None):
        if self.ended:
            raise RuntimeError("progress operation has ended")
        params = {"progress_id": self.id, "value": value}
        if label is not None:
            params["label"] = label
        return self._rpc.call("ui.progressUpdate", params)["updated"]

    def end(self):
        if not self.ended:
            self._rpc.call("ui.progressEnd", {"progress_id": self.id})
            self.ended = True

    def __enter__(self):
        return self

    def __exit__(self, exc_type, exc_value, traceback):
        self.end()
        return False


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

    def choose_open_file(self, title=None, filter=None):
        params = {}
        if title is not None:
            params["title"] = title
        if filter is not None:
            params["filter"] = filter
        return self._rpc.call("ui.chooseOpenFile", params)["path"]

    def choose_save_file(self, suggested_name=None, title=None, filter=None):
        params = {}
        if suggested_name is not None:
            params["suggested_name"] = suggested_name
        if title is not None:
            params["title"] = title
        if filter is not None:
            params["filter"] = filter
        return self._rpc.call("ui.chooseSaveFile", params)["path"]

    def progress(self, label, total=0):
        return Progress(
            self._rpc,
            self._rpc.call("ui.progressBegin", {"label": label, "total": total}),
        )


class EventsApi:
    def __init__(self, rpc, session_id=None):
        self._rpc = rpc
        self._session_id = session_id

    def subscribe(self, *events):
        return self._rpc.call("events.subscribe", {"events": list(events)})["subscribed"]

    def unsubscribe(self, *events):
        return self._rpc.call("events.unsubscribe", {"events": list(events)})["subscribed"]

    def _event(self, notification, include_self):
        if notification is None:
            return None
        params = notification.get("params", {})
        if (
            not include_self
            and self._session_id is not None
            and params.get("origin_session_id") == self._session_id
        ):
            return None
        return {"name": notification["method"], "params": params}

    def poll(self, include_self=False):
        while True:
            notification = self._rpc.poll_notification()
            if notification is None:
                return None
            event = self._event(notification, include_self)
            if event is not None:
                return event

    def next_event(self, include_self=False):
        """Wait for the next event; do not consume one RPC connection concurrently."""
        while True:
            event = self._event(self._rpc.next_notification(), include_self)
            if event is not None:
                return event


class Plugin:
    def __init__(self, rpc, session_info, transport):
        self._rpc = rpc
        self._transport = transport
        self.session_info = session_info
        self.book = BookApi(rpc)
        self.editor = EditorApi(rpc)
        self.validation = ValidationApi(rpc)
        self.input = InputApi(rpc)
        self.output = OutputApi(rpc)
        self.ui = UiApi(rpc)
        self.events = EventsApi(rpc, session_info.get("session_id"))

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
                "capabilities": {
                    "events": True,
                    "binary_chunks": True,
                    "text_chunks": True,
                    "position_encodings": ["utf-16"],
                },
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
