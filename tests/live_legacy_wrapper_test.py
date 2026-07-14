import pathlib
import sys
import tempfile
import unittest


LAUNCHER_ROOT = pathlib.Path(__file__).parents[1] / "src" / "Resource_Files" / "plugin_launchers" / "python"
sys.path.insert(0, str(LAUNCHER_ROOT))

from sigil_live.compat import LiveWrapper, WrapperException


PACKAGE = """<?xml version="1.0" encoding="utf-8"?>
<package xmlns="http://www.idpf.org/2007/opf" version="3.0" unique-identifier="bookid">
  <metadata xmlns:dc="http://purl.org/dc/elements/1.1/">
    <dc:identifier id="bookid">urn:test</dc:identifier>
    <dc:title>Test</dc:title>
    <dc:language>en</dc:language>
  </metadata>
  <manifest>
    <item id="chapter" href="Text/chapter.xhtml" media-type="application/xhtml+xml" />
    <item id="cover" href="Images/cover.png" media-type="image/png" />
  </manifest>
  <spine><itemref idref="chapter" /></spine>
</package>
"""


def resource(resource_id, book_path, media_type, resource_type, revision=1):
    return {
        "resource_id": resource_id,
        "book_path": book_path,
        "media_type": media_type,
        "resource_type": resource_type,
        "content_revision": revision,
        "loaded": True,
    }


class FakeTransaction:
    def __init__(self):
        self.active = True
        self.calls = []

    def replace_text(self, target, data, expected_revision=None):
        self.calls.append(("replace_text", target.book_path, data, expected_revision))

    def write_binary(self, target, data, expected_revision=None):
        self.calls.append(("write_binary", target.book_path, data, expected_revision))

    def add_resource(self, book_path, data, media_type, **options):
        self.calls.append(("add_resource", book_path, data, media_type, options))

    def remove_resource(self, target, expected_revision=None):
        self.calls.append(("remove_resource", target.book_path, expected_revision))

    def replace_package(self, data, expected_revision):
        self.calls.append(("replace_package", data, expected_revision))

    def replace_archive_file(self, book_path, data, expected_sha256):
        self.calls.append(("replace_archive_file", book_path, data, expected_sha256))

    def remove_archive_file(self, book_path, expected_sha256):
        self.calls.append(("remove_archive_file", book_path, expected_sha256))

    def commit(self):
        self.active = False
        self.calls.append(("commit",))
        return {"committed": True}

    def rollback(self):
        self.active = False
        self.calls.append(("rollback",))
        return {"rolled_back": True}


class FakeBook:
    def __init__(self):
        self.transactions = []
        self.text = {"OEBPS/content.opf": PACKAGE, "OEBPS/Text/chapter.xhtml": "<p>old</p>"}
        self.binary = {"OEBPS/Images/cover.png": b"png"}
        self.snapshot = {
            "package": {
                "resource": resource(
                    "opf", "OEBPS/content.opf", "application/oebps-package+xml", "opf", 7
                ),
                "text": PACKAGE,
                "book_path": "OEBPS/content.opf",
            },
            "resources": [
                resource("opf", "OEBPS/content.opf", "application/oebps-package+xml", "opf", 7),
                resource("chapter-r", "OEBPS/Text/chapter.xhtml", "application/xhtml+xml", "html", 3),
                resource("cover-r", "OEBPS/Images/cover.png", "image/png", "image", 4),
            ],
            "selected": ["OEBPS/Text/chapter.xhtml"],
            "font_mangling": {},
            "configuration": {
                "application_dir": "/app",
                "preferences_dir": "/prefs",
                "linux_hunspell_dictionary_dirs": [],
                "ui_language": "en",
                "spellcheck_language": "en_US",
                "color_mode": "light",
                "colors": {
                    "Window": "#ffffff",
                    "Base": "#ffffff",
                    "Text": "#000000",
                    "Highlight": "#0000ff",
                    "HighlightedText": "#ffffff",
                },
                "ui_font": "Sans,12",
                "using_automate": False,
                "automate_parameter": "",
            },
        }
        self.archive = {
            "mimetype": b"application/epub+zip",
            "META-INF/container.xml": b"<container/>",
            "META-INF/metadata.xml": b"<metadata/>",
        }

    def get_compatibility_snapshot(self):
        return self.snapshot

    def get_info(self):
        return {"modified": False, "file_path": "/books/test.epub"}

    def transaction(self, label, checkpoint="auto"):
        transaction = FakeTransaction()
        self.transactions.append((label, checkpoint, transaction))
        return transaction

    def archive_files(self, page_size=200):
        for book_path, data in self.archive.items():
            yield {
                "book_path": book_path,
                "size": len(data),
                "resource_id": None,
                "protected": book_path in ("mimetype", "META-INF/container.xml"),
            }

    def read_archive_file(self, book_path):
        return {"book_path": book_path, "data": self.archive[book_path], "sha256": "hash"}

    def read_text(self, target):
        return {"text": self.text[target.book_path], "revision": target.revision}

    def read_binary(self, target):
        return {"data": self.binary[target.book_path], "revision": target.revision}


class FakePlugin:
    def __init__(self):
        self.book = FakeBook()

    def ping(self):
        return True


class LiveLegacyWrapperTest(unittest.TestCase):
    def setUp(self):
        self.plugin = FakePlugin()
        self.wrapper = LiveWrapper(self.plugin, "/plugins/Test", "Test")

    def test_reads_live_text_and_binary_with_legacy_types(self):
        self.assertEqual(self.wrapper.readfile("chapter"), "<p>old</p>")
        self.assertEqual(self.wrapper.readfile("cover"), b"png")
        self.assertEqual(self.wrapper.get_opfbookpath(), "OEBPS/content.opf")
        self.assertEqual(self.wrapper.selected, ["OEBPS/Text/chapter.xhtml"])
        self.assertEqual(self.wrapper.readotherfile("mimetype"), b"application/epub+zip")
        self.assertEqual(self.wrapper.readotherfile("META-INF/container.xml"), b"<container/>")

    def test_stages_writes_additions_deletions_and_package_once(self):
        self.wrapper.writefile("chapter", "<p>new</p>")
        self.wrapper.writefile("cover", b"new-png")
        self.wrapper.addbookpath(
            "appendix", "OEBPS/Text/appendix.xhtml", "<p>appendix</p>",
            "application/xhtml+xml"
        )
        self.wrapper.deletefile("cover")
        self.assertEqual(self.wrapper.readfile("chapter"), "<p>new</p>")
        self.assertEqual(self.wrapper.readfile("appendix"), "<p>appendix</p>")

        result = self.wrapper.commit()
        self.assertEqual(result, {"committed": True})
        calls = self.plugin.book.transactions[0][2].calls
        self.assertEqual([call[0] for call in calls], [
            "replace_text", "remove_resource", "add_resource", "replace_package", "commit"
        ])
        self.assertEqual(calls[0][3], 3)
        self.assertNotIn("cover", self.wrapper.id_to_bookpath)
        self.assertIn('id="appendix"', calls[3][1])

    def test_unmanifested_resource_and_copy_use_staged_values(self):
        self.wrapper.addotherfile("META-INF/custom.xml", "<custom/>")
        with tempfile.TemporaryDirectory() as destination:
            self.wrapper.copy_book_contents_to(destination)
            copied = pathlib.Path(destination, "META-INF", "custom.xml")
            self.assertEqual(copied.read_text(encoding="utf-8"), "<custom/>")

    def test_existing_untracked_archive_files_use_fingerprint_transactions(self):
        self.wrapper.writeotherfile("META-INF/metadata.xml", "<metadata changed='1'/>")
        self.wrapper.commit()
        calls = self.plugin.book.transactions[0][2].calls
        self.assertEqual(calls[0], (
            "replace_archive_file", "META-INF/metadata.xml",
            b"<metadata changed='1'/>", "hash"
        ))

        plugin = FakePlugin()
        wrapper = LiveWrapper(plugin, "/plugins/Test", "Test")
        wrapper.deleteotherfile("META-INF/metadata.xml")
        wrapper.commit()
        self.assertEqual(plugin.book.transactions[0][2].calls[0], (
            "remove_archive_file", "META-INF/metadata.xml", "hash"
        ))

    def test_read_only_wrapper_rejects_mutation(self):
        wrapper = LiveWrapper(self.plugin, "/plugins/Test", "Test", writable=False)
        with self.assertRaises(WrapperException):
            wrapper.writefile("chapter", "changed")

    def test_added_other_paths_cannot_escape_the_book(self):
        with self.assertRaises(WrapperException):
            self.wrapper.addotherfile("../outside.txt", b"bad")


if __name__ == "__main__":
    unittest.main()
