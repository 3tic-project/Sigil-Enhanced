import pathlib
import sys
import unittest


LAUNCHER_ROOT = pathlib.Path(__file__).parents[1] / "src" / "Resource_Files" / "plugin_launchers" / "python"
sys.path.insert(0, str(LAUNCHER_ROOT))

from sigil_live.client import BookApi, EditorApi, EventsApi, InputApi, OutputApi, Resource, UiApi, ValidationApi
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
        self.assertEqual(rpc.poll_notification()["method"], "editor.contentChanged")

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

    def test_book_api_returns_one_resource_page_for_protocol_adapters(self):
        transport = FakeTransport([
            {"jsonrpc": "2.0", "id": 1, "result": {
                "items": [{
                    "resource_id": "css", "book_path": "Styles/book.css",
                    "media_type": "text/css", "resource_type": "css",
                    "content_revision": 2, "loaded": True,
                }],
                "next_cursor": "1",
            }},
        ])
        page = BookApi(RpcClient(transport)).list_resources(
            types=("css",), page_size=1, cursor="0"
        )
        self.assertEqual(page["items"][0].id, "css")
        self.assertEqual(page["next_cursor"], "1")
        self.assertEqual(transport.sent[0]["params"], {
            "page_size": 1, "types": ["css"], "cursor": "0",
        })

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

    def test_book_api_exposes_structured_package_sections(self):
        transport = FakeTransport(
            [
                {"jsonrpc": "2.0", "id": 1, "result": {"items": [{"name": "dc:title"}]}},
                {"jsonrpc": "2.0", "id": 2, "result": {"items": [{"id": "chapter"}]}},
                {"jsonrpc": "2.0", "id": 3, "result": {"items": [{"idref": "chapter"}]}},
                {"jsonrpc": "2.0", "id": 4, "result": {"items": []}},
                {"jsonrpc": "2.0", "id": 5, "result": {"items": []}},
            ]
        )
        book = BookApi(RpcClient(transport))
        self.assertEqual(book.get_metadata()["items"][0]["name"], "dc:title")
        self.assertEqual(book.get_manifest()[0]["id"], "chapter")
        self.assertEqual(book.get_spine()["items"][0]["idref"], "chapter")
        self.assertEqual(book.get_guide(), [])
        self.assertEqual(book.get_bindings(), [])

    def test_book_api_streams_binary_snapshots_and_closes_them(self):
        transport = FakeTransport(
            [
                {"jsonrpc": "2.0", "id": 1, "result": {
                    "stream_id": "stream", "resource_id": "image", "revision": 7,
                    "book_path": "Images/cover.png", "size": 5,
                    "sha256": "0" * 64, "chunk_size": 3,
                }},
                {"jsonrpc": "2.0", "id": 2, "result": {
                    "stream_id": "stream", "data_base64": "aGVs", "offset": 3, "eof": False,
                }},
                {"jsonrpc": "2.0", "id": 3, "result": {
                    "stream_id": "stream", "data_base64": "bG8=", "offset": 5, "eof": True,
                }},
                {"jsonrpc": "2.0", "id": 4, "result": {"closed": True}},
            ]
        )
        with BookApi(RpcClient(transport)).open_binary("image") as reader:
            self.assertEqual(reader.read(), b"hello")
            self.assertEqual((reader.size, reader.revision), (5, 7))
        self.assertEqual(
            [item["method"] for item in transport.sent],
            ["binary.openRead", "binary.readChunk", "binary.readChunk", "binary.close"],
        )

    def test_book_api_opens_unmanaged_archive_streams(self):
        transport = FakeTransport(
            [{"jsonrpc": "2.0", "id": 1, "result": {
                "stream_id": "stream", "resource_id": None, "revision": None,
                "book_path": "META-INF/large.bin", "size": 9,
                "sha256": "1" * 64, "chunk_size": 1024,
            }}]
        )
        reader = BookApi(RpcClient(transport)).open_archive_file("META-INF/large.bin")
        self.assertEqual(reader.book_path, "META-INF/large.bin")
        self.assertIsNone(reader.resource_id)
        self.assertEqual(transport.sent[0]["params"], {"book_path": "META-INF/large.bin"})

    def test_book_api_materializes_one_resource_without_a_target_path(self):
        transport = FakeTransport(
            [{"jsonrpc": "2.0", "id": 1, "result": {
                "path": "/tmp/session/image.png", "book_path": "Images/image.png",
                "resource_id": "image", "revision": 3, "size": 10, "sha256": "0" * 64,
            }}]
        )
        result = BookApi(RpcClient(transport)).materialize_temporary("image")
        self.assertEqual(result["path"], "/tmp/session/image.png")
        self.assertEqual(transport.sent[0]["params"], {"resource_id": "image"})

    def test_book_read_many_follows_continuation_tokens(self):
        transport = FakeTransport([
            {"jsonrpc": "2.0", "id": 1, "result": {
                "items": [{"resource_id": "a", "text": "A", "revision": 1}],
                "next_cursor": "1",
            }},
            {"jsonrpc": "2.0", "id": 2, "result": {
                "items": [{"resource_id": "b", "text": "B", "revision": 1}],
                "next_cursor": None,
            }},
        ])
        items = BookApi(RpcClient(transport)).read_many(["a", "b"])
        self.assertEqual([item["resource_id"] for item in items], ["a", "b"])
        self.assertEqual(transport.sent[1]["params"]["cursor"], "1")

    def test_book_and_transaction_read_bounded_text_ranges(self):
        transport = FakeTransport([
            {"jsonrpc": "2.0", "id": 1, "result": {
                "text": "abc", "start": 0, "end": 3, "next_start": 3,
            }},
            {"jsonrpc": "2.0", "id": 2, "result": {
                "text": "def", "start": 3, "end": 6, "next_start": None,
            }},
        ])
        rpc = RpcClient(transport)
        self.assertEqual(BookApi(rpc).read_text_range("chapter", 0, 3)["text"], "abc")
        from sigil_live.client import Transaction

        tx = Transaction(
            rpc,
            {"transaction_id": "tx", "base_book_revision": 1, "checkpoint": "auto"},
        )
        self.assertEqual(tx.read_text_range("new:1", 3, 3)["text"], "def")
        self.assertEqual(transport.sent[0]["method"], "resource.readTextRange")
        self.assertEqual(transport.sent[1]["method"], "transaction.readTextRange")
        self.assertEqual(transport.sent[1]["params"]["transaction_id"], "tx")

    def test_input_api_chunks_and_hashes_epub_uploads(self):
        transport = FakeTransport(
            [
                {"jsonrpc": "2.0", "id": 1, "result": {
                    "upload_id": "upload", "chunk_size": 4, "max_size": 100,
                }},
                {"jsonrpc": "2.0", "id": 2, "result": {"received": 4}},
                {"jsonrpc": "2.0", "id": 3, "result": {"received": 6}},
                {"jsonrpc": "2.0", "id": 4, "result": {"accepted": True}},
            ]
        )
        self.assertTrue(InputApi(RpcClient(transport)).submit_epub(
            b"PK\x03\x04ok", "converted.epub"
        )["accepted"])
        self.assertEqual(
            [item["method"] for item in transport.sent],
            ["input.beginEpub", "input.writeChunk", "input.writeChunk", "input.finishEpub"],
        )
        self.assertEqual(
            transport.sent[-1]["params"]["sha256"],
            "fb789d1e6f25f631f2a44d1cd635b3ba6e93708eca58ae6a12f6084af458c5ef",
        )

    def test_output_api_requests_host_epub_export(self):
        transport = FakeTransport(
            [
                {"jsonrpc": "2.0", "id": 1, "result": {
                    "exported": True, "path": "/tmp/output.epub", "mode": "copy",
                }},
                {"jsonrpc": "2.0", "id": 2, "result": {
                    "exported": True, "path": "/tmp/source.epub", "mode": "source",
                }},
            ]
        )
        output = OutputApi(RpcClient(transport))
        result = output.export_epub("/tmp/output.epub")
        self.assertTrue(result["exported"])
        self.assertEqual(transport.sent[0]["method"], "output.exportEpub")
        self.assertEqual(transport.sent[0]["params"], {"path": "/tmp/output.epub"})

        result = output.save_source()
        self.assertEqual(result["mode"], "source")
        self.assertEqual(transport.sent[1]["params"], {})

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

    def test_ui_progress_context_updates_and_ends(self):
        transport = FakeTransport(
            [
                {"jsonrpc": "2.0", "id": 1, "result": {"progress_id": "p", "total": 2}},
                {"jsonrpc": "2.0", "id": 2, "result": {"updated": True}},
                {"jsonrpc": "2.0", "id": 3, "result": {"ended": True}},
            ]
        )
        with UiApi(RpcClient(transport)).progress("Scanning", total=2) as progress:
            self.assertTrue(progress.update(1, "Chapter 1"))
        self.assertEqual(
            [item["method"] for item in transport.sent],
            ["ui.progressBegin", "ui.progressUpdate", "ui.progressEnd"],
        )

    def test_ui_file_dialogs_return_paths_or_none(self):
        transport = FakeTransport(
            [
                {"jsonrpc": "2.0", "id": 1, "result": {"path": "/tmp/input.epub"}},
                {"jsonrpc": "2.0", "id": 2, "result": {"path": None}},
            ]
        )
        ui = UiApi(RpcClient(transport))
        self.assertEqual(
            ui.choose_open_file("Open", "EPUB (*.epub)"), "/tmp/input.epub"
        )
        self.assertIsNone(ui.choose_save_file("output.epub"))

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

    def test_editor_selection_writes_include_current_state_token(self):
        state = {
            "active": True,
            "resource_id": "a",
            "book_path": "Text/a.xhtml",
            "revision": 2,
            "state_token": "state-1",
            "cursor": 4,
            "selection": {"start": 1, "end": 4, "text": "abc"},
            "position_encoding": "utf-16",
        }
        transport = FakeTransport([
            {"jsonrpc": "2.0", "id": 1, "result": state},
            {"jsonrpc": "2.0", "id": 2, "result": {
                "resource_id": "a", "revision": 3, "state_token": "state-2",
                "applied_edits": 1,
            }},
        ])
        result = EditorApi(RpcClient(transport)).replace_selection("replacement")
        self.assertEqual(result["state_token"], "state-2")
        self.assertEqual(transport.sent[0]["method"], "editor.getState")
        self.assertEqual(
            transport.sent[1]["params"]["expected_state_token"], "state-1"
        )

    def test_editor_navigation_uses_resource_ids_and_utf16_ranges(self):
        state = {
            "active": True, "resource_id": "a", "book_path": "Text/a.xhtml",
            "revision": 2, "cursor": 4,
            "selection": {"start": 1, "end": 4, "text": "abc"},
            "position_encoding": "utf-16",
        }
        transport = FakeTransport([
            {"jsonrpc": "2.0", "id": 1, "result": state},
            {"jsonrpc": "2.0", "id": 2, "result": state},
        ])
        editor = EditorApi(RpcClient(transport))
        editor.open_resource("a", position=4)
        editor.reveal_range("a", 1, 4)
        self.assertEqual(transport.sent[0]["method"], "editor.openResource")
        self.assertEqual(transport.sent[1]["params"]["end"], 4)

    def test_ui_api_returns_host_decisions(self):
        transport = FakeTransport([
            {"jsonrpc": "2.0", "id": 1, "result": {"shown": True}},
            {"jsonrpc": "2.0", "id": 2, "result": {"shown": True}},
            {"jsonrpc": "2.0", "id": 3, "result": {"confirmed": False}},
        ])
        ui = UiApi(RpcClient(transport))
        self.assertTrue(ui.show_status("Working", 1000))
        self.assertTrue(ui.show_message("Done", level="info"))
        self.assertFalse(ui.confirm("Continue?"))

    def test_events_api_subscribes_and_consumes_queued_notifications(self):
        transport = FakeTransport([
            {"jsonrpc": "2.0", "id": 1, "result": {
                "subscribed": ["editor.activeChanged"]
            }},
        ])
        rpc = RpcClient(transport)
        events = EventsApi(rpc)
        self.assertEqual(events.subscribe("editor.activeChanged"), ["editor.activeChanged"])
        rpc.notifications.append({
            "jsonrpc": "2.0", "method": "editor.activeChanged",
            "params": {"new_resource_id": "chapter"},
        })
        self.assertEqual(events.poll(), {
            "name": "editor.activeChanged",
            "params": {"new_resource_id": "chapter"},
        })

    def test_events_api_filters_self_origin_unless_requested(self):
        rpc = RpcClient(FakeTransport([]))
        events = EventsApi(rpc, "session-1")
        own = {
            "jsonrpc": "2.0", "method": "book.resourceChanged",
            "params": {"origin_session_id": "session-1"},
        }
        external = {
            "jsonrpc": "2.0", "method": "book.resourceChanged",
            "params": {"origin_session_id": None},
        }
        rpc.notifications.extend([own, external])
        self.assertIs(events.poll()["params"]["origin_session_id"], None)
        rpc.notifications.append(own)
        self.assertEqual(events.poll(include_self=True)["params"]["origin_session_id"], "session-1")

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

    def test_transaction_binary_writer_chunks_and_hashes_data(self):
        transport = FakeTransport([
            {"jsonrpc": "2.0", "id": 1, "result": {
                "upload_id": "upload", "chunk_size": 3, "max_size": 100,
            }},
            {"jsonrpc": "2.0", "id": 2, "result": {"received": 3}},
            {"jsonrpc": "2.0", "id": 3, "result": {"received": 5}},
            {"jsonrpc": "2.0", "id": 4, "result": {"staged": True}},
        ])
        from sigil_live.client import Transaction

        tx = Transaction(
            RpcClient(transport),
            {"transaction_id": "tx", "base_book_revision": 1, "checkpoint": "auto"},
        )
        writer = tx.begin_binary_write(
            Resource("image", "Images/a.png", "image/png", "image", 4, True), 5
        )
        writer.write(b"hello")
        self.assertTrue(writer.finish()["staged"])
        self.assertEqual(
            [item["method"] for item in transport.sent],
            ["transaction.writeBinaryBegin", "transaction.writeBinaryChunk",
             "transaction.writeBinaryChunk", "transaction.writeBinaryEnd"],
        )
        self.assertEqual(
            transport.sent[-1]["params"]["sha256"],
            "2cf24dba5fb0a30e26e83b2ac5b9e29e1b161e5c1fa7425e73043362938b9824",
        )

    def test_transaction_text_writer_chunks_unicode_by_utf8_size(self):
        transport = FakeTransport([
            {"jsonrpc": "2.0", "id": 1, "result": {
                "upload_id": "text-upload", "chunk_size": 4,
                "max_size": 100, "expected_size": 9,
            }},
            {"jsonrpc": "2.0", "id": 2, "result": {
                "received": 3, "duplicate": False,
            }},
            {"jsonrpc": "2.0", "id": 3, "result": {
                "received": 7, "duplicate": False,
            }},
            {"jsonrpc": "2.0", "id": 4, "result": {
                "received": 9, "duplicate": False,
            }},
            {"jsonrpc": "2.0", "id": 5, "result": {
                "staged": True, "resource_id": "chapter",
            }},
        ])
        from sigil_live.client import Transaction

        tx = Transaction(
            RpcClient(transport),
            {"transaction_id": "tx", "base_book_revision": 1, "checkpoint": "auto"},
        )
        writer = tx.begin_text_write("chapter", 9, expected_revision=4)
        self.assertEqual(writer.write("中文abc"), 9)
        self.assertTrue(writer.finish()["staged"])
        chunks = [
            item["params"] for item in transport.sent
            if item["method"] == "transaction.writeTextChunk"
        ]
        self.assertEqual([item["offset"] for item in chunks], [0, 3, 7])
        self.assertTrue(all(len(item["text"].encode("utf-8")) <= 4 for item in chunks))

    def test_transaction_text_add_upload_can_be_aborted(self):
        transport = FakeTransport([
            {"jsonrpc": "2.0", "id": 1, "result": {
                "upload_id": "text-add", "chunk_size": 1024,
                "max_size": 4096, "expected_size": 10,
            }},
            {"jsonrpc": "2.0", "id": 2, "result": {"aborted": True}},
        ])
        from sigil_live.client import Transaction

        tx = Transaction(
            RpcClient(transport),
            {"transaction_id": "tx", "base_book_revision": 1, "checkpoint": "auto"},
        )
        writer = tx.begin_text_add(
            "OEBPS/Text/new.xhtml", 10, "application/xhtml+xml",
            manifest_id="new_chapter",
        )
        self.assertTrue(writer.abort())
        self.assertEqual(transport.sent[0]["method"], "transaction.addTextBegin")
        self.assertEqual(transport.sent[1]["method"], "transaction.writeTextAbort")

    def test_transaction_updates_structured_metadata_and_spine(self):
        transport = FakeTransport([
            {"jsonrpc": "2.0", "id": 1, "result": {"revision": 7, "items": []}},
            {"jsonrpc": "2.0", "id": 2, "result": {"staged": True}},
            {"jsonrpc": "2.0", "id": 3, "result": {"revision": 7, "items": []}},
            {"jsonrpc": "2.0", "id": 4, "result": {"staged": True}},
        ])
        from sigil_live.client import Transaction

        tx = Transaction(
            RpcClient(transport),
            {"transaction_id": "tx", "base_book_revision": 1, "checkpoint": "auto"},
        )
        tx.update_metadata([{"name": "dc:title", "content": "Title"}])
        tx.update_spine([{"idref": "chapter", "linear": "yes"}], {"toc": "ncx"})
        self.assertEqual(transport.sent[1]["method"], "transaction.updateMetadata")
        self.assertEqual(transport.sent[3]["params"]["attributes"], {"toc": "ncx"})

    def test_archive_api_paginates_reads_and_stages_by_fingerprint(self):
        transport = FakeTransport(
            [
                {"jsonrpc": "2.0", "id": 1, "result": {
                    "items": [{"book_path": "mimetype"}], "next_cursor": None
                }},
                {"jsonrpc": "2.0", "id": 2, "result": {
                    "book_path": "mimetype", "data_base64": "YWJj", "sha256": "hash"
                }},
            ]
        )
        book = BookApi(RpcClient(transport))
        self.assertEqual(list(book.archive_files()), [{"book_path": "mimetype"}])
        self.assertEqual(book.read_archive_file("mimetype")["data"], b"abc")

        transport = FakeTransport([
            {"jsonrpc": "2.0", "id": 1, "result": {"staged": True}},
            {"jsonrpc": "2.0", "id": 2, "result": {"staged": True}},
        ])
        from sigil_live.client import Transaction
        tx = Transaction(
            RpcClient(transport),
            {"transaction_id": "tx", "base_book_revision": 1, "checkpoint": "auto"},
        )
        tx.replace_archive_file("META-INF/metadata.xml", b"new", "hash")
        tx.remove_archive_file("META-INF/signatures.xml", "hash2")
        self.assertEqual(
            [request["method"] for request in transport.sent],
            ["transaction.replaceArchiveFile", "transaction.removeArchiveFile"],
        )

    def test_validation_api_normalizes_result_locations(self):
        transport = FakeTransport([
            {"jsonrpc": "2.0", "id": 1, "result": {"accepted": 1}}
        ])
        result = ValidationApi(RpcClient(transport)).publish_results([
            {
                "type": "warning",
                "book_path": "OEBPS/Text/a.xhtml",
                "line": 4,
                "message": "Check this",
            }
        ])
        self.assertEqual(result, {"accepted": 1})
        self.assertEqual(transport.sent[0]["params"]["results"][0]["character"], -1)

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
