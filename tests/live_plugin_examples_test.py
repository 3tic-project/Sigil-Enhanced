import ast
import json
import pathlib
import re
import unittest
import xml.etree.ElementTree as ET


ROOT = pathlib.Path(__file__).parents[1]
EXAMPLES = ROOT / "examples" / "live_plugins"
CLIENT = ROOT / "src" / "Resource_Files" / "plugin_launchers" / "python" / "sigil_live" / "client.py"
COVERAGE = EXAMPLES / "api-coverage.json"

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


if __name__ == "__main__":
    unittest.main()
