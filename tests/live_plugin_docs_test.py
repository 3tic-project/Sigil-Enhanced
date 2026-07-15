import ast
import pathlib
import re
import unittest


ROOT = pathlib.Path(__file__).parents[1]
CLIENT = (
    ROOT
    / "src"
    / "Resource_Files"
    / "plugin_launchers"
    / "python"
    / "sigil_live"
    / "client.py"
)
DOCUMENTS = {
    "English": ROOT / "docs" / "LivePythonPluginAPI.md",
    "Chinese": ROOT / "docs" / "LivePythonPluginAPIReference.md",
}
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
# The launcher constructs the connection from private command-line values.
LAUNCHER_ONLY_METHODS = {"Plugin": {"connect"}}


def sdk_methods():
    tree = ast.parse(CLIENT.read_text(encoding="utf-8"))
    result = {}
    for node in tree.body:
        if not isinstance(node, ast.ClassDef) or node.name not in PUBLIC_CLASSES:
            continue
        methods = {
            item.name
            for item in node.body
            if isinstance(item, (ast.FunctionDef, ast.AsyncFunctionDef))
            and not item.name.startswith("_")
        }
        result[node.name] = methods - LAUNCHER_ONLY_METHODS.get(node.name, set())
    return result


class LivePluginDocsTest(unittest.TestCase):
    def test_both_references_cover_every_public_sdk_method(self):
        methods = sdk_methods()
        self.assertEqual(set(methods), PUBLIC_CLASSES)
        for language, path in DOCUMENTS.items():
            source = path.read_text(encoding="utf-8")
            for class_name, class_methods in methods.items():
                for method in class_methods:
                    pattern = re.compile(
                        r"`(?:[A-Za-z][A-Za-z0-9_]*\.)*"
                        + re.escape(method)
                        + r"\([^`]*\)`"
                    )
                    self.assertRegex(
                        source,
                        pattern,
                        "{0} reference does not document {1}.{2}".format(
                            language, class_name, method
                        ),
                    )

    def test_references_describe_current_trust_and_stream_limits(self):
        english = DOCUMENTS["English"].read_text(encoding="utf-8")
        chinese = DOCUMENTS["Chinese"].read_text(encoding="utf-8")
        self.assertIn("does not implement an RPC permission system", english)
        self.assertIn("不实现 RPC 方法级权限系统", chinese)
        self.assertNotIn("Navigation requires `ui.navigate`", english)
        self.assertNotIn("OPF writes are currently rejected", english)
        self.assertNotIn("reader for a binary resource of any size", english)
        self.assertNotIn("任意大小资源的 `BinaryReader`", chinese)
        for source in (english, chinese):
            self.assertIn("256 MiB", source)
            self.assertIn("1 GiB", source)


if __name__ == "__main__":
    unittest.main()
