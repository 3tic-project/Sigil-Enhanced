import pathlib
import sys


ROOT = pathlib.Path(__file__).parents[1]
PLUGIN_ROOT = ROOT / "examples" / "live_plugins" / "SigilMcpServer"
LAUNCHER_ROOT = ROOT / "src" / "Resource_Files" / "plugin_launchers" / "python"
sys.path.insert(0, str(PLUGIN_ROOT))
sys.path.insert(0, str(LAUNCHER_ROOT))

from sigil_live.client import EditorState, Resource, Selection


class FakeTextWriter:
    def __init__(self, identifier="text-upload"):
        self.id = identifier
        self.chunk_size = 1024
        self.max_size = 64 * 1024 * 1024
        self.expected_size = 12
        self.received = 0
        self.finished = False

    def write(self, text, offset=None):
        encoded = text.encode("utf-8")
        if offset is not None and offset != self.received:
            return self.received
        self.received += len(encoded)
        return self.received

    def finish(self):
        self.finished = True
        return {"staged": True, "resource_id": "new:1", "size": self.received}

    def abort(self):
        self.finished = True
        return True


class FakeTransaction:
    def __init__(self, identifier="transaction-1"):
        self.id = identifier
        self.base_book_revision = 9
        self.checkpoint = "auto"
        self.active = True
        self.calls = []

    def _result(self, name, *args, **kwargs):
        self.calls.append((name, args, kwargs))
        return {"operation": name, "transaction_id": self.id}

    def read_text(self, resource_id):
        self.calls.append(("read_text", (resource_id,), {}))
        return {"resource_id": resource_id, "text": "staged", "revision": 3}

    def read_text_range(self, resource_id, start=0, max_utf16_units=1024 * 1024):
        return {
            "target_id": resource_id,
            "text": "staged"[start:start + max_utf16_units],
            "start": start,
            "revision": 3,
        }

    def begin_text_write(self, resource_id, size, expected_revision=None):
        writer = FakeTextWriter("text-write")
        writer.expected_size = size
        return writer

    def begin_text_add(self, book_path, size, media_type, **kwargs):
        writer = FakeTextWriter("text-add")
        writer.expected_size = size
        return writer

    def replace_text(self, *args, **kwargs):
        return self._result("replace_text", *args, **kwargs)

    def apply_edits(self, *args, **kwargs):
        return self._result("apply_edits", *args, **kwargs)

    def add_resource(self, *args, **kwargs):
        return self._result("add_resource", *args, **kwargs)

    def write_binary_file(self, *args, **kwargs):
        path = pathlib.Path(args[1])
        self.calls.append(
            ("write_binary_file", (args[0], path.read_bytes(), *args[2:]), kwargs)
        )
        return {"operation": "write_binary_file", "transaction_id": self.id}

    def remove_resource(self, *args, **kwargs):
        return self._result("remove_resource", *args, **kwargs)

    def move_resource(self, *args, **kwargs):
        return self._result("move_resource", *args, **kwargs)

    def rename_resource(self, *args, **kwargs):
        return self._result("rename_resource", *args, **kwargs)

    def replace_package(self, *args, **kwargs):
        return self._result("replace_package", *args, **kwargs)

    def update_metadata(self, *args, **kwargs):
        return self._result("update_metadata", *args, **kwargs)

    def update_spine(self, *args, **kwargs):
        return self._result("update_spine", *args, **kwargs)

    def preview(self):
        self.calls.append(("preview", (), {}))
        return {
            "transaction_id": self.id,
            "valid": True,
            "summary": {"modified": 2, "added": 1, "deleted": 0, "renamed": 1},
            "conflicts": [],
        }

    def validate(self):
        self.calls.append(("validate", (), {}))
        return {"transaction_id": self.id, "valid": True, "conflicts": []}

    def commit(self):
        self.calls.append(("commit", (), {}))
        self.active = False
        return {"committed": True, "transaction_id": self.id}

    def rollback(self):
        self.calls.append(("rollback", (), {}))
        self.active = False
        return {"rolled_back": True, "transaction_id": self.id}


class FakeBook:
    def __init__(self):
        self.transactions = []
        self.resources = [
            Resource(
                "chapter",
                "Text/chapter.xhtml",
                "application/xhtml+xml",
                "html",
                3,
                True,
            ),
            Resource("style", "Styles/book.css", "text/css", "css", 2, True),
        ]

    def get_info(self):
        return {
            "epub_version": "3.0",
            "modified": True,
            "file_path": "/books/example.epub",
            "revision": 9,
        }

    def get_metadata(self):
        return {"revision": 2, "items": [{"name": "dc:title", "content": "Example"}]}

    def get_manifest(self):
        return [{"id": "chapter", "book_path": "Text/chapter.xhtml"}]

    def get_spine(self):
        return {"revision": 2, "items": [{"idref": "chapter"}]}

    def get_guide(self):
        return []

    def get_bindings(self):
        return []

    def list_resources(self, types=None, page_size=100, cursor=None):
        offset = int(cursor or 0)
        items = [item for item in self.resources if not types or item.resource_type in types]
        page = items[offset:offset + page_size]
        next_offset = offset + len(page)
        return {
            "items": page,
            "next_cursor": str(next_offset) if next_offset < len(items) else None,
        }

    def read_text(self, resource_id):
        return {"text": "<p>Current editor text</p>", "revision": 3}

    def read_text_range(self, resource_id, start=0, max_utf16_units=1024 * 1024):
        text = "<p>Current editor text</p>"
        return {
            "target_id": resource_id,
            "text": text[start:start + max_utf16_units],
            "start": start,
            "end": min(len(text), start + max_utf16_units),
            "revision": 3,
        }

    def read_many(self, resource_ids):
        return [
            {"resource_id": item, "text": "text:" + item, "revision": 1}
            for item in resource_ids
        ]

    def read_many_page(self, resource_ids, cursor=None):
        offset = int(cursor or 0)
        items = self.read_many(resource_ids[offset:offset + 1])
        next_offset = offset + len(items)
        return {
            "items": items,
            "next_cursor": str(next_offset) if next_offset < len(resource_ids) else None,
        }

    def transaction(self, label, checkpoint):
        transaction = FakeTransaction("transaction-{0}".format(len(self.transactions) + 1))
        transaction.label = label
        transaction.checkpoint = checkpoint
        self.transactions.append(transaction)
        return transaction


class FakeEditor:
    def __init__(self):
        self.last_call = None

    def get_state(self):
        return EditorState(
            active=True,
            resource_id="chapter",
            book_path="Text/chapter.xhtml",
            revision=3,
            state_token="editor-state-1",
            cursor=5,
            selection=Selection(2, 5, "abc"),
        )

    def get_open_tabs(self):
        return [Resource("chapter", "Text/chapter.xhtml", "application/xhtml+xml", "html", 3, True)]

    def open_resource(self, resource_id, position=None):
        return self.get_state()

    def reveal_range(self, resource_id, start, end):
        return self.get_state()

    def apply_edits(self, edits, **kwargs):
        return {"applied": len(edits), "revision": 4}

    def replace_selection(self, text, **kwargs):
        self.last_call = ("replace_selection", text, kwargs)
        return {"applied": True, "revision": 4, "text": text}

    def insert_text(self, text, **kwargs):
        self.last_call = ("insert_text", text, kwargs)
        return {"applied": True, "revision": 4, "text": text}


class FakeUi:
    def __init__(self):
        self.confirmations = []
        self.confirm_result = True
        self.status = []
        self.messages = []

    def confirm(self, message, title=None):
        self.confirmations.append((message, title))
        return self.confirm_result

    def show_status(self, message, duration_ms=5000):
        self.status.append((message, duration_ms))
        return True

    def show_message(self, message, title=None, level="info"):
        self.messages.append((message, title, level))
        return True


class FakePlugin:
    def __init__(self, runtime_directory=None):
        self.session_info = {
            "session_id": "session-test",
            "protocol_version": 1,
            "api_version": 2,
            "lifetime": "book-session",
            "position_encoding": "utf-16",
            "max_message_size": 8 * 1024 * 1024,
            "runtime_directory": str(runtime_directory) if runtime_directory else "",
        }
        self.book = FakeBook()
        self.editor = FakeEditor()
        self.ui = FakeUi()
        self.pings = 0

    def ping(self):
        self.pings += 1
        return True
