import base64
import os
import stat
import tempfile
import time
import unittest

from sigil_mcp_test_support import FakePlugin
from sigil_mcp.backend import SigilMcpBackend
from sigil_mcp.catalog import PROMPT_NAMES, RESOURCE_URIS, TOOL_NAMES
from sigil_mcp.errors import BackendError, error_payload
from sigil_mcp.rendezvous import RendezvousFile


class SigilMcpBackendTest(unittest.TestCase):
    def setUp(self):
        self.plugin = FakePlugin()
        self.backend = SigilMcpBackend(self.plugin, idle_timeout_seconds=60)

    def test_session_and_capabilities_do_not_expose_rendezvous_path(self):
        info = self.backend.session_info()
        self.assertEqual(info["book"]["revision"], 9)
        self.assertNotIn("runtime_directory", str(info))
        capabilities = self.backend.capabilities()
        self.assertEqual(tuple(capabilities["tools"]), TOOL_NAMES)
        self.assertEqual(tuple(capabilities["resources"]), RESOURCE_URIS)
        self.assertEqual(tuple(capabilities["prompts"]), PROMPT_NAMES)

    def test_resource_page_and_editor_state_are_json_serializable_values(self):
        page = self.backend.resource_list(types=["html"], page_size=1)
        self.assertEqual(page["items"][0]["id"], "chapter")
        self.assertIsNone(page["next_cursor"])
        state = self.backend.editor_state()
        self.assertEqual(state["selection"], {"start": 2, "end": 5, "text": "abc"})
        self.assertEqual(state["position_encoding"], "utf-16")

    def test_transaction_rejects_foreign_handle_and_direct_commit_clears_state(self):
        started = self.backend.transaction_begin("Generate chapter", "auto")
        transaction_id = started["transaction_id"]
        with self.assertRaises(BackendError):
            self.backend.transaction_preview("foreign")
        staged = self.backend.transaction_replace_text(
            transaction_id, "chapter", 3, "updated"
        )
        self.assertEqual(staged["operation"], "replace_text")
        result = self.backend.transaction_commit(transaction_id)
        self.assertTrue(result["committed"])
        self.assertFalse(result["confirmation_required"])
        self.assertNotIn("confirmed", result)
        self.assertEqual(self.plugin.ui.confirmations, [])
        with self.assertRaises(BackendError):
            self.backend.transaction_preview(transaction_id)

    def test_binary_resource_decodes_strict_base64_before_staging(self):
        transaction_id = self.backend.transaction_begin()["transaction_id"]
        result = self.backend.transaction_add_binary_resource(
            transaction_id,
            "OEBPS/Images/cover.jpg",
            base64.b64encode(b"\xff\xd8jpeg\xff\xd9").decode("ascii"),
            "image/jpeg",
        )
        self.assertEqual(result["operation"], "add_resource")
        call = self.plugin.book.transactions[0].calls[-1]
        self.assertEqual(call[1][1], b"\xff\xd8jpeg\xff\xd9")
        self.assertFalse(call[2]["add_to_spine"])
        with self.assertRaises(BackendError):
            self.backend.transaction_add_binary_resource(
                transaction_id, "OEBPS/Images/bad.jpg", "not base64!", "image/jpeg"
            )

    def test_commit_does_not_consult_the_ui_confirmation_default(self):
        transaction_id = self.backend.transaction_begin()["transaction_id"]
        self.plugin.ui.confirm_result = False
        result = self.backend.transaction_commit(transaction_id)
        self.assertTrue(result["committed"])
        self.assertFalse(result["confirmation_required"])
        self.assertEqual(self.plugin.ui.confirmations, [])
        with self.assertRaises(BackendError):
            self.backend.transaction_status()

    def test_idle_and_shutdown_paths_roll_back(self):
        self.backend.transaction_begin()
        self.backend._transaction_activity = time.monotonic() - 61
        self.assertTrue(self.backend.expire_idle_transaction())
        self.assertTrue(self.plugin.book.transactions[0].calls[-1][0] == "rollback")

        self.backend.transaction_begin()
        self.assertTrue(self.backend.shutdown())
        self.assertEqual(self.plugin.book.transactions[1].calls[-1][0], "rollback")

    def test_expected_errors_are_machine_readable(self):
        payload = error_payload(BackendError(
            "Busy", "writer busy", retryable=True, recovery="rollback first"
        ))
        self.assertEqual(payload["code"], "Busy")
        self.assertTrue(payload["retryable"])
        self.assertEqual(payload["recovery"], "rollback first")


class RendezvousTest(unittest.TestCase):
    def test_atomic_owner_only_metadata_and_owned_cleanup(self):
        with tempfile.TemporaryDirectory() as directory:
            rendezvous = RendezvousFile({
                "session_id": "session/test",
                "runtime_directory": directory,
            })
            path = rendezvous.write({"token": "secret", "endpoint": "http://127.0.0.1/mcp"})
            self.assertTrue(path.is_file())
            self.assertNotIn("/", path.name)
            self.assertEqual(list(path.parent.glob("*.tmp")), [])
            if os.name != "nt":
                self.assertEqual(stat.S_IMODE(path.stat().st_mode), 0o600)
                self.assertEqual(stat.S_IMODE(path.parent.stat().st_mode), 0o700)
            self.assertTrue(rendezvous.remove())
            self.assertFalse(path.exists())

    def test_cleanup_does_not_remove_metadata_replaced_by_another_owner(self):
        with tempfile.TemporaryDirectory() as directory:
            rendezvous = RendezvousFile({
                "session_id": "session",
                "runtime_directory": directory,
            })
            path = rendezvous.write({"token": "first"})
            path.write_text('{"token":"second"}\n', encoding="utf-8")
            self.assertFalse(rendezvous.remove())
            self.assertTrue(path.exists())

    def test_unwritten_owner_does_not_remove_an_existing_file(self):
        with tempfile.TemporaryDirectory() as directory:
            session = {"session_id": "session", "runtime_directory": directory}
            owner = RendezvousFile(session)
            path = owner.write({"token": "first"})
            unwritten = RendezvousFile(session)
            self.assertFalse(unwritten.remove())
            self.assertTrue(path.exists())

    def test_metadata_requires_a_token(self):
        with tempfile.TemporaryDirectory() as directory:
            rendezvous = RendezvousFile({
                "session_id": "session", "runtime_directory": directory,
            })
            with self.assertRaises(ValueError):
                rendezvous.write({"endpoint": "http://127.0.0.1/mcp"})


if __name__ == "__main__":
    unittest.main()
