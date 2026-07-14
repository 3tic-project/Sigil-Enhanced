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
    def test_native_sdk_does_not_eagerly_load_legacy_containers(self):
        self.assertNotIn("bookcontainer", sys.modules)
        self.assertNotIn("pluginhunspell", sys.modules)

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

    def test_book_api_requests_compatibility_snapshot(self):
        snapshot = {"package": {"text": "<package/>", "book_path": "content.opf"}}
        transport = FakeTransport(
            [{"jsonrpc": "2.0", "id": 1, "result": snapshot}]
        )
        self.assertIs(
            BookApi(RpcClient(transport)).get_compatibility_snapshot(), snapshot
        )
        self.assertEqual(
            transport.sent[0]["method"], "book.getCompatibilitySnapshot"
        )

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

    def test_text_transaction_uses_one_id_until_commit(self):
        transport = FakeTransport(
            [
                {
                    "jsonrpc": "2.0",
                    "id": 1,
                    "result": {
                        "transaction_id": "tx-1",
                        "base_book_revision": 8,
                        "checkpoint": "auto",
                    },
                },
                {"jsonrpc": "2.0", "id": 2, "result": {"staged": True}},
                {"jsonrpc": "2.0", "id": 3, "result": {"modified": 1}},
            ]
        )
        tx = BookApi(RpcClient(transport)).transaction("Normalize")
        tx.replace_text("chapter", "updated", expected_revision=4)
        result = tx.commit()
        self.assertEqual(result["modified"], 1)
        self.assertFalse(tx.active)
        self.assertEqual(transport.sent[1]["params"]["transaction_id"], "tx-1")
        self.assertEqual(transport.sent[2]["method"], "transaction.commit")

    def test_binary_api_encodes_and_decodes_base64(self):
        transport = FakeTransport(
            [
                {
                    "jsonrpc": "2.0",
                    "id": 1,
                    "result": {"resource_id": "image", "revision": 2, "data_base64": "AAEC"},
                }
            ]
        )
        result = BookApi(RpcClient(transport)).read_binary("image")
        self.assertEqual(result["data"], b"\x00\x01\x02")

        transport = FakeTransport(
            [{"jsonrpc": "2.0", "id": 1, "result": {"staged": True}}]
        )
        from sigil_live.client import Transaction

        tx = Transaction(
            RpcClient(transport),
            {"transaction_id": "tx", "base_book_revision": 1, "checkpoint": "auto"},
        )
        tx.write_binary("image", b"\x00\xff", expected_revision=2)
        self.assertEqual(transport.sent[0]["method"], "transaction.writeBinary")
        self.assertEqual(transport.sent[0]["params"]["data_base64"], "AP8=")

    def test_structure_transaction_maps_resource_operations(self):
        responses = [
            {"jsonrpc": "2.0", "id": index, "result": {"staged": True}}
            for index in range(1, 6)
        ]
        transport = FakeTransport(responses)
        from sigil_live.client import Transaction

        tx = Transaction(
            RpcClient(transport),
            {"transaction_id": "tx", "base_book_revision": 1, "checkpoint": "auto"},
        )
        tx.add_resource(
            "OEBPS/Text/new.xhtml",
            "<html/>",
            "application/xhtml+xml",
            manifest_id="new_chapter",
        )
        tx.replace_package("<package/>", expected_revision=3)
        resource = Resource(
            "resource", "OEBPS/Text/old.xhtml", "application/xhtml+xml", "html", 4, True
        )
        tx.rename_resource(resource, "renamed.xhtml")
        tx.move_resource(resource, "OEBPS/Appendix/renamed.xhtml")
        tx.remove_resource(resource)
        self.assertEqual(
            [request["method"] for request in transport.sent],
            [
                "transaction.addResource",
                "transaction.replacePackage",
                "transaction.renameResource",
                "transaction.moveResource",
                "transaction.removeResource",
            ],
        )
        self.assertEqual(transport.sent[0]["params"]["text"], "<html/>")
        self.assertEqual(transport.sent[1]["params"]["expected_revision"], 3)
        self.assertEqual(transport.sent[2]["params"]["expected_revision"], 4)


if __name__ == "__main__":
    unittest.main()
