import json
import pathlib
import subprocess
import sys
import tempfile
import unittest
import zipfile
import xml.etree.ElementTree as ET


ROOT = pathlib.Path(__file__).parents[1]
SKILL = ROOT / "examples" / "codex_skills" / "sigil-epub-layout"
INSPECT_EPUB = SKILL / "scripts" / "inspect_epub.py"
INSPECT_SOURCE = SKILL / "scripts" / "inspect_source.py"
VALIDATE_EPUB = SKILL / "scripts" / "validate_epub.py"


CONTAINER = """<?xml version="1.0" encoding="utf-8"?>
<container xmlns="urn:oasis:names:tc:opendocument:xmlns:container" version="1.0">
  <rootfiles><rootfile full-path="OEBPS/content.opf" media-type="application/oebps-package+xml"/></rootfiles>
</container>
"""

PACKAGE = """<?xml version="1.0" encoding="utf-8"?>
<package xmlns="http://www.idpf.org/2007/opf" version="3.0" unique-identifier="BookId">
  <metadata xmlns:dc="http://purl.org/dc/elements/1.1/">
    <dc:identifier id="BookId">urn:uuid:00000000-0000-4000-8000-000000000001</dc:identifier>
    <dc:title>Fixture</dc:title><dc:creator>Tester</dc:creator><dc:language>zh-CN</dc:language>
    <meta property="dcterms:modified">2026-01-01T00:00:00Z</meta>
  </metadata>
  <manifest>
    <item id="cover" href="Text/cover.xhtml" media-type="application/xhtml+xml"/>
    <item id="chapter" href="Text/chapter.xhtml" media-type="application/xhtml+xml"/>
    <item id="nav" href="Text/nav.xhtml" media-type="application/xhtml+xml" properties="nav"/>
    <item id="style" href="Styles/book.css" media-type="text/css"/>
    <item id="cover-image" href="Images/cover.jpg" media-type="image/jpeg" properties="cover-image"/>
  </manifest>
  <spine><itemref idref="cover"/><itemref idref="chapter"/><itemref idref="nav" linear="no"/></spine>
</package>
"""

HTML_HEAD = """<?xml version="1.0" encoding="utf-8"?>
<html xmlns="http://www.w3.org/1999/xhtml" xmlns:epub="http://www.idpf.org/2007/ops" lang="zh-CN">
<head><title>{0}</title><link rel="stylesheet" href="../Styles/book.css"/></head><body>{1}</body></html>
"""


def build_epub(path, empty_chapter=False, toc_role=True):
    with zipfile.ZipFile(path, "w") as archive:
        archive.writestr("mimetype", b"application/epub+zip", compress_type=zipfile.ZIP_STORED)
        archive.writestr("META-INF/container.xml", CONTAINER)
        archive.writestr("OEBPS/content.opf", PACKAGE)
        archive.writestr(
            "OEBPS/Text/cover.xhtml",
            HTML_HEAD.format("Cover", '<img src="../Images/cover.jpg" alt="Cover"/>'),
        )
        archive.writestr(
            "OEBPS/Text/chapter.xhtml",
            "" if empty_chapter else HTML_HEAD.format("Chapter", "<h1>Chapter</h1><p>Text</p>"),
        )
        archive.writestr(
            "OEBPS/Text/nav.xhtml",
            HTML_HEAD.format(
                "Contents",
                (
                    '<nav epub:type="toc"{0}><ol>'
                    '<li><a href="chapter.xhtml">Chapter</a></li></ol></nav>'
                ).format(' role="doc-toc"' if toc_role else ""),
            ),
        )
        archive.writestr("OEBPS/Styles/book.css", "p { text-indent: 2em; }")
        archive.writestr("OEBPS/Images/cover.jpg", b"\xff\xd8fixture\xff\xd9")


class SigilEpubLayoutSkillTest(unittest.TestCase):
    def test_skill_is_explicit_only_and_has_no_placeholders(self):
        skill = (SKILL / "SKILL.md").read_text(encoding="utf-8")
        workflow = (SKILL / "references" / "mcp-workflow.md").read_text(
            encoding="utf-8"
        )
        agent = (SKILL / "agents" / "openai.yaml").read_text(encoding="utf-8")
        self.assertNotIn("TODO", skill)
        self.assertIn("explicitly invokes `$sigil-epub-layout`", skill)
        self.assertIn("allow_implicit_invocation: false", agent)
        self.assertIn("$sigil-epub-layout", agent)
        self.assertIn("Single-Transaction Creation", workflow)
        self.assertNotIn("Two-Phase Creation", workflow)
        self.assertLess(
            workflow.index("3. Add generated XHTML/CSS"),
            workflow.index("6. Call `update_spine`"),
        )

    def test_template_assets_are_well_formed_and_css_is_minimal(self):
        bundled = [path for path in SKILL.rglob("*") if path.is_file()]
        self.assertFalse(any("__pycache__" in path.parts for path in bundled))
        self.assertFalse(any(path.suffix.lower() in {".epub", ".otf", ".ttf"} for path in bundled))
        for path in (SKILL / "assets").glob("*.xhtml"):
            ET.fromstring(path.read_text(encoding="utf-8"))
        css = (SKILL / "assets" / "reflowable-horizontal.css").read_text(encoding="utf-8")
        self.assertIn("writing-mode: horizontal-tb", css)
        self.assertIn("ruby rt", css)
        self.assertNotIn("@font-face", css)
        self.assertNotIn("duokan-", css)

    def test_inspect_and_validate_minimal_epub(self):
        with tempfile.TemporaryDirectory() as directory:
            epub = pathlib.Path(directory) / "fixture.epub"
            build_epub(epub)
            inspected = subprocess.run(
                [sys.executable, str(INSPECT_EPUB), str(epub)],
                check=True,
                capture_output=True,
                text=True,
            )
            report = json.loads(inspected.stdout)
            self.assertEqual(report["epub_version"], "3.0")
            self.assertEqual(report["counts"]["manifest"], 5)
            self.assertEqual(report["navigation"]["nav_paths"], ["OEBPS/Text/nav.xhtml"])

            validated = subprocess.run(
                [sys.executable, str(VALIDATE_EPUB), str(epub), "--strict-layout", "--json"],
                check=True,
                capture_output=True,
                text=True,
            )
            self.assertTrue(json.loads(validated.stdout)["valid"])

    def test_validator_rejects_empty_spine_text(self):
        with tempfile.TemporaryDirectory() as directory:
            epub = pathlib.Path(directory) / "empty.epub"
            build_epub(epub, empty_chapter=True)
            result = subprocess.run(
                [sys.executable, str(VALIDATE_EPUB), str(epub), "--strict-layout", "--json"],
                check=False,
                capture_output=True,
                text=True,
            )
            self.assertEqual(result.returncode, 1)
            self.assertIn("text resource is empty", "\n".join(json.loads(result.stdout)["errors"]))

    def test_validator_rejects_toc_without_document_role(self):
        with tempfile.TemporaryDirectory() as directory:
            epub = pathlib.Path(directory) / "missing-role.epub"
            build_epub(epub, toc_role=False)
            result = subprocess.run(
                [sys.executable, str(VALIDATE_EPUB), str(epub), "--strict-layout", "--json"],
                check=False,
                capture_output=True,
                text=True,
            )
            self.assertEqual(result.returncode, 1)
            self.assertIn("role=doc-toc", "\n".join(json.loads(result.stdout)["errors"]))

    def test_source_inspection_does_not_emit_prose(self):
        with tempfile.TemporaryDirectory() as directory:
            source = pathlib.Path(directory)
            prose = "SECRET PROSE MUST NOT APPEAR"
            (source / "book.txt").write_text(
                "［插图：cover］\n[ruby=よみ]本文[/ruby]\n正文（註１）\n註１：说明\n" + prose,
                encoding="utf-8",
            )
            result = subprocess.run(
                [sys.executable, str(INSPECT_SOURCE), str(source)],
                check=True,
                capture_output=True,
                text=True,
            )
            report = json.loads(result.stdout)
            self.assertEqual(report["text"][0]["image_markers"], ["cover"])
            self.assertEqual(report["text"][0]["ruby_markers"], 1)
            self.assertEqual(report["text"][0]["footnotes"]["references"], 1)
            self.assertEqual(report["text"][0]["footnotes"]["definitions"], 1)
            self.assertEqual(report["text"][0]["footnotes"]["missing_definitions"], [])
            self.assertNotIn(prose, result.stdout)


if __name__ == "__main__":
    unittest.main()
