"""Read a fixed repository made by repomanager's two-checkpoint workflow."""

import importlib.util
import os
import sys
import tempfile
import unittest
import warnings
import zipfile
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
sys.path[:0] = [
    str(ROOT / "src/Resource_Files/python3lib"),
    str(ROOT / "src/Resource_Files/plugin_launchers/python"),
]
if importlib.util.find_spec("dulwich") is None:
    print("checkpoint golden test unavailable: dulwich is not installed", file=sys.stderr)
    sys.exit(77)
import repomanager as legacy


BOOK_ID = "fixture-book"
FIXTURE = ROOT / "tests/fixtures/checkpoint_legacy_repo.zip"
EXPECTED_DIFF = '''diff --git a/OEBPS/Text/次.xhtml b/OEBPS/Text/次.xhtml
new file mode 100644
index 0000000..c6d5f7f
--- /dev/null
+++ b/OEBPS/Text/次.xhtml
@@ -0,0 +1 @@
+<p>次</p>
diff --git a/OEBPS/Text/章.xhtml b/OEBPS/Text/章.xhtml
index 9a1b649..946fac6 100644
--- a/OEBPS/Text/章.xhtml
+++ b/OEBPS/Text/章.xhtml
@@ -1 +1 @@
-<p>一</p>
+<p>二</p>
'''.encode("utf-8")
EXPECTED_LOG = '''V0002 Sun Jun 15 2025 15:07:40 +0000
    Tag: V0002

V0001 Sun Jun 15 2025 15:06:40 +0000
    初始版本



--------------------------------------------------
commit: 4d008ac6342bd0feb524bbb067cc8d94dacd82aa
Author: Sigil <sigil@sigil-ebook.com>
Date:   Sun Jun 15 2025 15:07:40 +0000

updating to V0002

 OEBPS/Text/次.xhtml |  1 +
 OEBPS/Text/章.xhtml |  2 +-
 2 files changed, 2 insertions(+), 1 deletions(-)


--------------------------------------------------
commit: 093756ec60c16e77d63929303b730ccda68d73b9
Author: Sigil <sigil@sigil-ebook.com>
Date:   Sun Jun 15 2025 15:06:40 +0000

Initial Commit

 META-INF/container.xml  |  1 +
 META-INF/encryption.xml |  1 +
 META-INF/rights.xml     |  1 +
 OEBPS/Text/章.xhtml    |  1 +
 OEBPS/content.opf       |  1 +
 mimetype                |  1 +
 6 files changed, 6 insertions(+), 0 deletions(-)


'''


class CheckpointLegacyGoldenTest(unittest.TestCase):
    def setUp(self):
        self.original_cwd = Path.cwd()
        self.temporary = tempfile.TemporaryDirectory()
        self.base = Path(self.temporary.name)
        self.repos = self.base / "repos"
        self.repo = self.repos / f"epub_{BOOK_ID}"
        self.repo.mkdir(parents=True)
        with zipfile.ZipFile(FIXTURE) as archive:
            archive.extractall(self.repo)

    def tearDown(self):
        # The legacy checkpoint diff changes the process cwd and forgets to
        # restore it. Keep the test isolated without changing that behavior.
        os.chdir(self.original_cwd)
        self.temporary.cleanup()

    def test_annotated_tags_and_log(self):
        self.assertEqual(
            legacy.get_tag_list(str(self.repos), BOOK_ID),
            [
                'V0001|Sun Jun 15 2025 15:06:40 +0000|初始版本\n',
                'V0002|Sun Jun 15 2025 15:07:40 +0000|Tag: V0002\n',
            ],
        )
        self.assertEqual(legacy.generate_log_summary(str(self.repos), BOOK_ID), EXPECTED_LOG)
        self.assertEqual(
            (self.repo / ".bookinfo").read_text(encoding="utf-8"),
            "demo.epub\n示例书\n2026-01-02\nV0002\nfixture-book\n",
        )

    def test_commit_diff_and_parsed_text_diff(self):
        self.assertEqual(
            legacy.generate_diff_from_checkpoints(
                str(self.repos), BOOK_ID, "V0001", "V0002",
            ),
            EXPECTED_DIFF,
        )
        first = self.base / "first.xhtml"
        first.write_text('<p>一</p>\n', encoding="utf-8")
        second = self.repo / "OEBPS/Text/章.xhtml"
        with warnings.catch_warnings():
            # The legacy helper does not explicitly close its input handles.
            warnings.simplefilter("ignore", ResourceWarning)
            parsed = legacy.generate_parsed_ndiff(str(first), str(second))
        self.assertEqual(
            parsed,
            [('3', '<p>一</p>', '<p>二</p>', '   ^\n', '   ^\n')],
        )

    def test_checkout_and_epub_export(self):
        checkout = self.base / "checkout"
        checkout.mkdir()
        copied = legacy.copy_tag_to_destdir(
            str(self.repos), BOOK_ID, "V0001", str(checkout),
        )
        self.assertEqual(
            set(copied.splitlines()),
            {
                "mimetype", "META-INF/container.xml", "META-INF/rights.xml",
                "META-INF/encryption.xml", "OEBPS/content.opf",
                "OEBPS/Text/章.xhtml",
            },
        )
        self.assertEqual((checkout / "OEBPS/Text/章.xhtml").read_text(), '<p>一</p>\n')
        self.assertEqual((self.repo / "OEBPS/Text/章.xhtml").read_text(), '<p>二</p>\n')

        path = legacy.generate_epub_from_tag(
            str(self.repos), BOOK_ID, "V0001", "demo", str(self.base),
        )
        self.assertEqual(Path(path).name, "demo_V0001.epub")
        with zipfile.ZipFile(path) as exported:
            entries = exported.infolist()
            self.assertEqual(entries[0].filename, "mimetype")
            self.assertEqual(entries[0].compress_type, zipfile.ZIP_STORED)
            self.assertEqual(exported.read("mimetype"), b"application/epub+zip")
            self.assertEqual(exported.read("OEBPS/Text/章.xhtml"), '<p>一</p>\n'.encode())
            self.assertEqual(
                {entry.filename for entry in entries},
                {"mimetype", "META-INF/container.xml", "OEBPS/content.opf",
                 "OEBPS/Text/章.xhtml"},
            )
        self.assertEqual((self.repo / "OEBPS/Text/章.xhtml").read_text(), '<p>二</p>\n')


if __name__ == "__main__":
    unittest.main()
