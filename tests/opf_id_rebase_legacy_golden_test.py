"""Freeze the old, full-serialization manifest ID rebasing result."""

import sys
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
sys.path[:0] = [
    str(ROOT / "tests/fixtures"),
    str(ROOT / "src/Resource_Files/plugin_launchers/python"),
]
from fix_opf_ids_legacy import GenerateBaseIdFromFilename, GetValidId, rebase_manifest_ids


SOURCE = '''<?xml version="1.0" encoding="UTF-8"?>
<package xmlns="http://www.idpf.org/2007/opf" xmlns:dc="http://purl.org/dc/elements/1.1/" version="3.0" unique-identifier="bookid">
  <!-- keep comment -->
  <metadata><dc:identifier id="bookid">urn:test</dc:identifier><meta name="cover" content="cover-old"/><meta property="x" refines="#chapter-old">refined</meta></metadata>
  <manifest><item id="cover-old" href="Images/%E5%B0%81%E9%9D%A2.jpg" media-type="image/jpeg"/><item id="chapter-old" href="Text/%E7%AC%AC%E4%B8%80%E7%AB%A0.xhtml" media-type="application/xhtml+xml" media-overlay="audio-old"/><item id="second-old" href="Extra/%E7%AC%AC%E4%B8%80%E7%AB%A0.xhtml" media-type="application/xhtml+xml" fallback="chapter-old"/><item id="audio-old" href="Audio/narration.mp3" media-type="audio/mpeg"/><item id="ncx-old" href="toc.ncx" media-type="application/x-dtbncx+xml"/></manifest>
  <spine toc="ncx-old"><itemref idref="chapter-old"/><itemref idref="second-old"/></spine>
  <bindings><mediaType media-type="application/x-demo" handler="second-old"/></bindings>
</package>'''

EXPECTED = '''<?xml version="1.0" encoding="utf-8"?>
<package version="3.0" unique-identifier="bookid" xmlns="http://www.idpf.org/2007/opf" xmlns:dc="http://purl.org/dc/elements/1.1/">
  <metadata>
    <dc:identifier id="bookid">urn:test</dc:identifier>
    <meta name="cover" content="FengMian_jpg"/>
    <meta property="x" refines="#DiYiZhang_xhtml">refined</meta>
  </metadata>
  <manifest>
    <item id="FengMian_jpg" href="Images/封面.jpg" media-type="image/jpeg"/>
    <item id="DiYiZhang_xhtml" href="Text/第一章.xhtml" media-type="application/xhtml+xml" media-overlay="narration_mp3"/>
    <item id="DiYiZhang_xhtml0001" href="Extra/第一章.xhtml" media-type="application/xhtml+xml" fallback="DiYiZhang_xhtml"/>
    <item id="narration_mp3" href="Audio/narration.mp3" media-type="audio/mpeg"/>
    <item id="toc_ncx" href="toc.ncx" media-type="application/x-dtbncx+xml"/>
  </manifest>
  <spine toc="toc_ncx">
    <itemref idref="DiYiZhang_xhtml"/>
    <itemref idref="DiYiZhang_xhtml0001"/>
  </spine>
  <bindings>
  <mediaType media-type="application/x-demo" handler="DiYiZhang_xhtml0001"/>
  </bindings>
</package>
'''


class OpfIdRebaseLegacyGoldenTest(unittest.TestCase):
    def test_filename_to_id_rules(self):
        self.assertEqual(GetValidId(GenerateBaseIdFromFilename("封面.jpg")), "FengMian_jpg")
        self.assertEqual(GetValidId(GenerateBaseIdFromFilename("123 Cover.png")),
                         "x123Cover_png")

    def test_rebase_updates_all_references(self):
        self.assertEqual(rebase_manifest_ids(SOURCE), EXPECTED)
        # This full serialization drops comments and decodes hrefs. It is the
        # legacy baseline only for preservation-off mode. Preservation-on will
        # need a separate source-patch assertion during the C++ migration.
        self.assertIn("<!-- keep comment -->", SOURCE)
        self.assertNotIn("<!-- keep comment -->", EXPECTED)


if __name__ == "__main__":
    unittest.main()
