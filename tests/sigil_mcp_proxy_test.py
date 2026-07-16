import importlib.util
import json
import pathlib
import tempfile
import unittest
from unittest import mock


ROOT = pathlib.Path(__file__).parents[1]
PROXY = ROOT / "src" / "Resource_Files" / "plugin_launchers" / "python" / "sigil_mcp_stdio_proxy.py"
SPEC = importlib.util.spec_from_file_location("sigil_mcp_stdio_proxy", PROXY)
MODULE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(MODULE)


class SigilMcpProxyTest(unittest.TestCase):
    def metadata(self, session_id="session"):
        return {
            "endpoint": "http://127.0.0.1:54321/mcp",
            "token": "secret",
            "session_id": session_id,
            "transport": "streamable-http",
            "book": {"file_path": "/books/example.epub"},
        }

    def test_explicit_metadata_is_loaded_without_exposing_token_in_errors(self):
        with tempfile.TemporaryDirectory() as directory:
            path = pathlib.Path(directory) / "sigil-mcp-session.json"
            path.write_text(json.dumps(self.metadata()), encoding="utf-8")
            selected, metadata = MODULE.discover_metadata(metadata_path=path)
            self.assertEqual(selected, path)
            self.assertEqual(metadata["session_id"], "session")

    def test_discovery_requires_explicit_selection_for_multiple_books(self):
        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory)
            for index in range(2):
                path = root / "sigil-mcp-{0}.json".format(index)
                path.write_text(
                    json.dumps(self.metadata("session-{0}".format(index))),
                    encoding="utf-8",
                )
            with self.assertRaises(MODULE.ProxyError) as raised:
                MODULE.discover_metadata(runtime_directory=root)
            self.assertIn("Multiple", str(raised.exception))
            self.assertNotIn("secret", str(raised.exception))

            selected, metadata = MODULE.discover_metadata(
                runtime_directory=root, session_id="session-1"
            )
            self.assertEqual(metadata["session_id"], "session-1")
            self.assertEqual(selected.name, "sigil-mcp-1.json")

    def test_macos_discovery_matches_qstandardpaths_runtime_location(self):
        with tempfile.TemporaryDirectory() as directory:
            home = pathlib.Path(directory)
            expected = home / "Library/Application Support/sigil-enhanced/mcp"
            expected.mkdir(parents=True)
            metadata_path = expected / "sigil-mcp-session.json"
            metadata_path.write_text(json.dumps(self.metadata()), encoding="utf-8")

            with mock.patch.object(MODULE.sys, "platform", "darwin"), \
                 mock.patch.object(MODULE.pathlib.Path, "home", return_value=home), \
                 mock.patch.dict(MODULE.os.environ, {}, clear=True), \
                 mock.patch.object(MODULE.tempfile, "gettempdir", return_value=str(home / "tmp")):
                selected, metadata = MODULE.discover_metadata()

            self.assertEqual(selected, metadata_path)
            self.assertEqual(metadata["session_id"], "session")

    def test_endpoint_validation_rejects_remote_or_ambiguous_urls(self):
        self.assertEqual(
            MODULE.validate_endpoint("http://127.0.0.1:1234/mcp").hostname,
            "127.0.0.1",
        )
        for endpoint in (
            "https://127.0.0.1:1234/mcp",
            "http://example.com:1234/mcp",
            "http://127.0.0.1:1234/other",
            "http://user:password@127.0.0.1:1234/mcp",
        ):
            with self.subTest(endpoint=endpoint):
                with self.assertRaises(MODULE.ProxyError):
                    MODULE.validate_endpoint(endpoint)

    def test_metadata_contract_is_validated(self):
        with tempfile.TemporaryDirectory() as directory:
            path = pathlib.Path(directory) / "sigil-mcp-invalid.json"
            path.write_text('{"token":"secret"}', encoding="utf-8")
            with self.assertRaises(MODULE.ProxyError) as raised:
                MODULE.discover_metadata(metadata_path=path)
            self.assertIn("missing", str(raised.exception))


if __name__ == "__main__":
    unittest.main()
