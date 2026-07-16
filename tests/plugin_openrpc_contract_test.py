import json
import pathlib
import re
import unittest


ROOT = pathlib.Path(__file__).parents[1]
SCHEMA_PATH = ROOT / "docs" / "plugin-api-v2.openrpc.json"
DISPATCH_PATH = ROOT / "src" / "PluginAPI" / "PluginSession.cpp"
TEXT_RESOURCE_PATH = ROOT / "src" / "ResourceObjects" / "TextResource.cpp"
GUMBO_INTERFACE_PATH = ROOT / "src" / "Parsers" / "GumboInterface.cpp"
HTML_RESOURCE_PATH = ROOT / "src" / "ResourceObjects" / "HTMLResource.cpp"

IMPLEMENTED_METHODS = {
    "session.hello",
    "session.ping",
    "session.getInfo",
    "session.finish",
    "events.subscribe",
    "events.unsubscribe",
    "book.getInfo",
    "book.getMetadata",
    "book.getManifest",
    "book.getSpine",
    "book.getGuide",
    "book.getBindings",
    "book.getSelection",
    "book.getCompatibilitySnapshot",
    "book.getRevision",
    "validation.publishResults",
    "archive.listFiles",
    "archive.readFile",
    "resource.list",
    "resource.resolvePath",
    "resource.getInfo",
    "resource.readText",
    "resource.readTextRange",
    "resource.readBinary",
    "resource.readMany",
    "resource.materializeTemporary",
    "binary.openRead",
    "binary.readChunk",
    "binary.close",
    "input.beginEpub",
    "input.writeChunk",
    "input.finishEpub",
    "output.exportEpub",
    "editor.getState",
    "editor.getSelection",
    "editor.getOpenTabs",
    "editor.applyEdits",
    "editor.replaceSelection",
    "editor.insertText",
    "editor.setCursor",
    "editor.setSelection",
    "editor.openResource",
    "editor.revealRange",
    "ui.showStatus",
    "ui.showMessage",
    "ui.confirm",
    "ui.progressBegin",
    "ui.progressUpdate",
    "ui.progressEnd",
    "ui.chooseOpenFile",
    "ui.chooseSaveFile",
    "transaction.begin",
    "transaction.readText",
    "transaction.readTextRange",
    "transaction.writeTextBegin",
    "transaction.addTextBegin",
    "transaction.writeTextChunk",
    "transaction.writeTextEnd",
    "transaction.writeTextAbort",
    "transaction.readBinary",
    "transaction.writeBinary",
    "transaction.writeBinaryBegin",
    "transaction.writeBinaryChunk",
    "transaction.writeBinaryEnd",
    "transaction.replaceArchiveFile",
    "transaction.removeArchiveFile",
    "transaction.addResource",
    "transaction.removeResource",
    "transaction.moveResource",
    "transaction.renameResource",
    "transaction.replacePackage",
    "transaction.updateMetadata",
    "transaction.updateSpine",
    "transaction.replaceText",
    "transaction.applyTextEdits",
    "transaction.validate",
    "transaction.preview",
    "transaction.commit",
    "transaction.rollback",
}


class OpenRpcContractTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.schema = json.loads(SCHEMA_PATH.read_text(encoding="utf-8"))

    def test_schema_lists_every_implemented_method_once(self):
        names = [method["name"] for method in self.schema["methods"]]
        self.assertEqual(len(names), len(set(names)))
        self.assertEqual(set(names), IMPLEMENTED_METHODS)

    def test_dispatcher_and_schema_do_not_drift(self):
        source = DISPATCH_PATH.read_text(encoding="utf-8")
        dispatched = set(
            re.findall(r'method\s*[!=]=\s*QStringLiteral\("([A-Za-z0-9.]+)"\)', source)
        )
        self.assertEqual(dispatched, IMPLEMENTED_METHODS)

    def test_error_codes_match_protocol_contract(self):
        errors = self.schema["components"]["errors"]
        self.assertEqual(
            {value["code"] for value in errors.values()},
            set(range(-32012, -32001)),
        )

    def test_all_local_schema_references_resolve(self):
        schemas = self.schema["components"]["schemas"]

        def walk(value):
            if isinstance(value, dict):
                reference = value.get("$ref")
                if reference is not None:
                    prefix = "#/components/schemas/"
                    self.assertTrue(reference.startswith(prefix))
                    self.assertIn(reference[len(prefix) :], schemas)
                for child in value.values():
                    walk(child)
            elif isinstance(value, list):
                for child in value:
                    walk(child)

        walk(self.schema)

    def test_lazy_mcp_reads_cannot_notify_an_unready_editor(self):
        text_resource = TEXT_RESOURCE_PATH.read_text(encoding="utf-8")
        initial_load = re.search(
            r"void TextResource::InitialLoad\(\)\s*\{(?P<body>.*?)\n\}",
            text_resource,
            re.DOTALL,
        )
        self.assertIsNotNone(initial_load)
        self.assertIn("QSignalBlocker blocker(m_TextDocument)", initial_load["body"])

        gumbo = GUMBO_INTERFACE_PATH.read_text(encoding="utf-8")
        self.assertIn("return m_output ? m_output->document : NULL;", gumbo)
        self.assertIn("return m_output ? m_output->root : NULL;", gumbo)
        self.assertRegex(
            gumbo,
            r"GumboNode\* node = get_root_node\(\);\s*if \(!node\) \{\s*return NULL;",
        )

    def test_added_html_is_loaded_before_it_can_be_saved(self):
        dispatcher = DISPATCH_PATH.read_text(encoding="utf-8")
        self.assertRegex(
            dispatcher,
            r"AddContentFileToFolder\([\s\S]*?\);\s*"
            r"if \(auto \*text_resource = qobject_cast<TextResource \*>\(resource\)\) \{\s*"
            r"text_resource->InitialLoad\(\);",
        )

        html_resource = HTML_RESOURCE_PATH.read_text(encoding="utf-8")
        save = re.search(
            r"void HTMLResource::SaveToDisk\(bool book_wide_save\)\s*\{(?P<body>.*?)\n\}",
            html_resource,
            re.DOTALL,
        )
        self.assertIsNotNone(save)
        self.assertLess(
            save["body"].index("InitialLoad();"),
            save["body"].index("SetText(GetText());"),
        )


if __name__ == "__main__":
    unittest.main()
