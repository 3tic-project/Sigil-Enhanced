import pathlib
import sys
import unittest


ROOT = pathlib.Path(__file__).parents[1]
PLUGIN_ROOT = ROOT / "examples" / "live_plugins" / "SigilMcpServer"
sys.path.insert(0, str(PLUGIN_ROOT))

from sigil_mcp.catalog import PROMPT_NAMES, RESOURCE_URIS, TOOL_NAMES


class SigilMcpDocsTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.system = (ROOT / "docs" / "MCPSystem.md").read_text(encoding="utf-8")
        cls.guide = (ROOT / "docs" / "MCPUserGuide_zh-CN.md").read_text(encoding="utf-8")
        cls.reference = (ROOT / "docs" / "MCPToolsReference_zh-CN.md").read_text(
            encoding="utf-8"
        )
        cls.readme = (PLUGIN_ROOT / "README.md").read_text(encoding="utf-8")

    def test_every_tool_is_in_architecture_and_api_reference(self):
        for name in TOOL_NAMES:
            with self.subTest(tool=name):
                self.assertIn("`" + name + "`", self.system)
                self.assertIn(name, self.reference)

    def test_every_resource_and_prompt_is_documented(self):
        for uri in RESOURCE_URIS:
            with self.subTest(resource=uri):
                self.assertIn(uri, self.system)
                self.assertIn(uri, self.reference)
        for name in PROMPT_NAMES:
            with self.subTest(prompt=name):
                self.assertIn(name, self.system)
                self.assertIn(name, self.reference)

    def test_user_docs_cover_both_transports_and_security_boundaries(self):
        combined = self.guide + self.readme
        for text in (
            "Streamable HTTP",
            "stdio",
            "bearer token",
            "RevisionConflict",
            "sigil.transaction.preview",
            "sigil.transaction.validate",
            "sigil.transaction.commit",
            "sigil.transaction.rollback",
        ):
            with self.subTest(text=text):
                self.assertIn(text, combined)

    def test_packaging_pins_the_stable_mcp_sdk(self):
        for name in ("requirements-core.txt", "winreqs.txt"):
            requirements = (
                ROOT / "src" / "Resource_Files" / "python_pkg" / name
            ).read_text(encoding="utf-8")
            self.assertIn("mcp==1.28.1", requirements)
        self.assertIn("mcp==1.28.1", self.system)

    def test_tracked_design_matches_the_implemented_proxy(self):
        self.assertIn("bundled standard-library stdio proxy", self.system)
        self.assertNotIn("stdio proxy is deferred", self.system)

    def test_commit_docs_match_direct_commit_behavior(self):
        self.assertIn("without a native confirmation", self.system)
        self.assertIn("不再显示 Sigil 确认对话框", self.guide)
        self.assertIn('"confirmation_required": false', self.reference)
        self.assertIn("without a confirmation dialog", self.readme)

    def test_external_import_is_documented_as_a_token_free_data_path(self):
        for document in (self.system, self.guide, self.reference, self.readme):
            with self.subTest(document=document[:30]):
                self.assertIn("/api/v1/imports", document)
                self.assertIn("sigil_mcp_upload.py", document)
        self.assertIn("external_binary_add_size_max", self.reference)
        self.assertIn("batch_uploader_path", self.reference)
        self.assertIn("batch_uploader_path", self.guide)
        self.assertIn("--manifest imports.json --check", self.guide)
        self.assertIn("服务端不接受 `source_path`", self.reference)


if __name__ == "__main__":
    unittest.main()
