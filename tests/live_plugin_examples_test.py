import ast
import importlib.util
import json
import pathlib
import re
import subprocess
import sys
import tempfile
import unittest
import xml.etree.ElementTree as ET
import zipfile


ROOT = pathlib.Path(__file__).parents[1]
EXAMPLES = ROOT / "examples" / "live_plugins"
CLIENT = ROOT / "src" / "Resource_Files" / "plugin_launchers" / "python" / "sigil_live" / "client.py"
COVERAGE = EXAMPLES / "api-coverage.json"
PACKAGER = EXAMPLES / "package_plugin.py"
MCP_PLUGIN = EXAMPLES / "SigilMcpServer"
MCP_ARCHIVE = EXAMPLES / "SigilMcpServer.zip"

PUBLIC_CLASSES = {
    "BinaryReader",
    "InputWriter",
    "InputApi",
    "OutputApi",
    "Transaction",
    "BookApi",
    "EditorApi",
    "ValidationApi",
    "Progress",
    "UiApi",
    "EventsApi",
    "Plugin",
}
LIFECYCLE_METHODS = {"Plugin": {"connect", "finish", "close"}}


def sdk_methods():
    tree = ast.parse(CLIENT.read_text(encoding="utf-8"))
    result = {}
    for node in tree.body:
        if not isinstance(node, ast.ClassDef) or node.name not in PUBLIC_CLASSES:
            continue
        methods = {
            item.name for item in node.body
            if isinstance(item, (ast.FunctionDef, ast.AsyncFunctionDef))
            and not item.name.startswith("_")
        }
        result[node.name] = methods - LIFECYCLE_METHODS.get(node.name, set())
    return result


def load_packager():
    spec = importlib.util.spec_from_file_location("package_live_plugin", PACKAGER)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


class LivePluginExamplesTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.coverage = json.loads(COVERAGE.read_text(encoding="utf-8"))

    def test_every_example_is_installable_and_compiles(self):
        names = set()
        plugin_dirs = sorted(path.parent for path in EXAMPLES.glob("*/plugin.xml"))
        self.assertGreaterEqual(len(plugin_dirs), 6)
        for plugin_dir in plugin_dirs:
            root = ET.parse(plugin_dir / "plugin.xml").getroot()
            self.assertEqual(root.tag, "plugin")
            name = root.findtext("name")
            self.assertTrue(name)
            self.assertNotIn(name, names)
            names.add(name)
            self.assertIn(root.findtext("type"), {"edit", "input", "output", "validation"})
            self.assertEqual(root.findtext("engine"), "python3")
            api = root.find("api")
            self.assertIsNotNone(api)
            self.assertEqual((api.get("version"), api.get("interface")), ("2", "live"))
            self.assertIn(root.findtext("lifetime"), {"command", "book-session"})
            source_path = plugin_dir / "plugin.py"
            compile(source_path.read_text(encoding="utf-8"), str(source_path), "exec")

    def test_coverage_manifest_tracks_every_public_sdk_method(self):
        expected = sdk_methods()
        actual = {name: set(methods) for name, methods in self.coverage.items()}
        self.assertEqual(actual, expected)
        for class_name, methods in self.coverage.items():
            for method, relative_path in methods.items():
                source_path = EXAMPLES / relative_path
                self.assertTrue(source_path.is_file(), relative_path)
                source = source_path.read_text(encoding="utf-8")
                self.assertRegex(
                    source,
                    re.compile(r"\." + re.escape(method) + r"\s*\("),
                    "{0}.{1} is not called by {2}".format(
                        class_name, method, relative_path
                    ),
                )

    def assert_archive_matches_plugin(self, archive_path, plugin_dir):
        packager = load_packager()
        expected = {
            archive_name.as_posix(): source.read_bytes()
            for source, archive_name in packager.plugin_files(plugin_dir)
        }
        with zipfile.ZipFile(archive_path) as archive:
            names = archive.namelist()
            self.assertEqual(names, sorted(names))
            self.assertEqual(set(names), set(expected))
            self.assertIn(plugin_dir.name + "/plugin.xml", names)
            self.assertTrue(all(name.startswith(plugin_dir.name + "/") for name in names))
            self.assertFalse(
                any(
                    part.startswith(".")
                    for name in names
                    for part in pathlib.PurePosixPath(name).parts
                )
            )
            self.assertFalse(any("__MACOSX" in name for name in names))
            self.assertFalse(any("__pycache__" in name for name in names))
            self.assertFalse(any(name.endswith((".pyc", ".pyo", ".zip")) for name in names))
            for name, payload in expected.items():
                self.assertEqual(archive.read(name), payload, name)

    def test_packager_creates_deterministic_installer_layout(self):
        with tempfile.TemporaryDirectory() as workspace:
            destination = pathlib.Path(workspace) / "SigilMcpServer.zip"
            subprocess.run(
                [sys.executable, str(PACKAGER), str(MCP_PLUGIN), str(destination)],
                check=True,
                capture_output=True,
                text=True,
            )
            first = destination.read_bytes()
            self.assert_archive_matches_plugin(destination, MCP_PLUGIN)
            subprocess.run(
                [sys.executable, str(PACKAGER), str(MCP_PLUGIN), str(destination)],
                check=True,
                capture_output=True,
                text=True,
            )
            self.assertEqual(destination.read_bytes(), first)

    def test_bundled_mcp_archive_is_current_and_installable(self):
        self.assertTrue(MCP_ARCHIVE.is_file())
        self.assert_archive_matches_plugin(MCP_ARCHIVE, MCP_PLUGIN)


if __name__ == "__main__":
    unittest.main()
