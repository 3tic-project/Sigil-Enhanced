import pathlib
import sys
import unittest


LAUNCHER_ROOT = pathlib.Path(__file__).parents[1] / "src" / "Resource_Files" / "plugin_launchers" / "python"
sys.path.insert(0, str(LAUNCHER_ROOT))

from sigil_live.client import BookApi, EditorApi, Resource
from sigil_live.errors import PermissionDenied
from sigil_live.rpc import RpcClient


class FakeTransport:
    def __init__(self, responses):
        self.responses = list(responses)
        self.sent = []

    def send(self, message):
        self.sent.append(message)

    def receive(self):
        return self.responses.pop(0)


class LiveSdkTest(unittest.TestCase):
    def test_rpc_keeps_notifications_and_returns_matching_result(self):
        transport = FakeTransport(
            [
                {"jsonrpc": "2.0", "method": "editor.contentChanged", "params": {}},
                {"jsonrpc": "2.0", "id": 1, "result": {"pong": True}},
            ]
        )
        rpc = RpcClient(transport)
        self.assertEqual(rpc.call("session.ping"), {"pong": True})
        self.assertEqual(transport.sent[0]["id"], 1)
        self.assertEqual(len(rpc.notifications), 1)

    def test_rpc_maps_host_errors(self):
        rpc = RpcClient(
            FakeTransport(
                [{"jsonrpc": "2.0", "id": 1, "error": {"code": -32001, "message": "denied"}}]
            )
        )
        with self.assertRaises(PermissionDenied):
            rpc.call("resource.readText")

    def test_book_api_paginates_and_builds_resources(self):
        rpc = RpcClient(
            FakeTransport(
                [
                    {
                        "jsonrpc": "2.0",
                        "id": 1,
                        "result": {
                            "items": [
                                {
                                    "resource_id": "a",
                                    "book_path": "Text/a.xhtml",
                                    "media_type": "application/xhtml+xml",
                                    "resource_type": "html",
                                    "content_revision": 4,
                                    "loaded": True,
                                }
                            ],
                            "next_cursor": "1",
                        },
                    },
                    {"jsonrpc": "2.0", "id": 2, "result": {"items": [], "next_cursor": None}},
                ]
            )
        )
        resources = list(BookApi(rpc).resources(types=("html",), page_size=1))
        self.assertEqual(resources, [Resource("a", "Text/a.xhtml", "application/xhtml+xml", "html", 4, True)])

    def test_editor_selection_is_typed(self):
        rpc = RpcClient(
            FakeTransport(
                [
                    {
                        "jsonrpc": "2.0",
                        "id": 1,
                        "result": {
                            "active": True,
                            "resource_id": "a",
                            "book_path": "Text/a.xhtml",
                            "revision": 2,
                            "cursor": 5,
                            "selection": {"start": 2, "end": 5, "text": "abc"},
                            "position_encoding": "utf-16",
                        },
                    }
                ]
            )
        )
        selection = EditorApi(rpc).get_selection()
        self.assertEqual((selection.start, selection.end, selection.text), (2, 5, "abc"))

    def test_editor_apply_edits_preserves_utf16_ranges(self):
        transport = FakeTransport(
            [
                {
                    "jsonrpc": "2.0",
                    "id": 1,
                    "result": {"resource_id": "a", "revision": 3, "applied_edits": 1},
                }
            ]
        )
        result = EditorApi(RpcClient(transport)).apply_edits(
            [(1, 3, "x")], expected_revision=2, resource_id="a"
        )
        self.assertEqual(result["revision"], 3)
        self.assertEqual(
            transport.sent[0]["params"]["edits"], [{"start": 1, "end": 3, "text": "x"}]
        )


if __name__ == "__main__":
    unittest.main()
