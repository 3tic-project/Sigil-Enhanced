import json
import pathlib
import re
import unittest


ROOT = pathlib.Path(__file__).parents[1]
SCHEMA_PATH = ROOT / "docs" / "plugin-api-v2.openrpc.json"
DISPATCH_PATH = ROOT / "src" / "PluginAPI" / "PluginSession.cpp"

IMPLEMENTED_METHODS = {
    "session.hello",
    "session.ping",
    "session.getInfo",
    "session.finish",
    "events.subscribe",
    "events.unsubscribe",
    "book.getInfo",
    "book.getCompatibilitySnapshot",
    "book.getRevision",
    "validation.publishResults",
    "archive.listFiles",
    "archive.readFile",
    "resource.list",
    "resource.resolvePath",
    "resource.getInfo",
    "resource.readText",
    "resource.readBinary",
    "resource.readMany",
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
    "transaction.begin",
    "transaction.readText",
    "transaction.readBinary",
    "transaction.writeBinary",
    "transaction.replaceArchiveFile",
    "transaction.removeArchiveFile",
    "transaction.addResource",
    "transaction.removeResource",
    "transaction.moveResource",
    "transaction.renameResource",
    "transaction.replacePackage",
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
            set(range(-32012, -32000)),
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


if __name__ == "__main__":
    unittest.main()
