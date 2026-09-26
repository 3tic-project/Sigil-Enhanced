"""Freeze NCX output from the current nav.xhtml generator."""

import sys
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
sys.path[:0] = [
    str(ROOT / "tests/fixtures"),
    str(ROOT / "src/Resource_Files/plugin_launchers/python"),
]
from ncxgenerator_legacy import generateGuideEntries, generateNCX


NAV = '''<?xml version="1.0" encoding="utf-8"?>
<html xmlns="http://www.w3.org/1999/xhtml" xmlns:epub="http://www.idpf.org/2007/ops"><head><title>Nav</title></head><body>
<nav epub:type="toc"><ol><li><a href="Text/one.xhtml">One</a><ol><li><a href="Text/two%20words.xhtml#part%201">Part Two</a></li></ol></li><li><a href="#end">End</a></li></ol></nav>
<nav epub:type="page-list"><ol><li><a href="Text/one.xhtml#p1">1</a></li></ol></nav>
<nav epub:type="landmarks"><ol><li><a epub:type="cover" href="Text/cover.xhtml">Cover</a></li><li><a epub:type="chapter" href="Text/one.xhtml">Chapter</a></li><li><a epub:type="unmapped" href="Text/one.xhtml">Other</a></li></ol></nav>
</body></html>'''

EXPECTED_NCX = '''<?xml version="1.0" encoding="utf-8"?>
<ncx xmlns="http://www.daisy.org/z3986/2005/ncx/" version="2005-1">
  <head>
    <meta name="dtb:uid" content="urn:book" />
    <meta name="dtb:depth" content="2" />
    <meta name="dtb:totalPageCount" content="1" />
    <meta name="dtb:maxPageNumber" content="1" />
  </head>
<docTitle>
  <text>Book &amp; Tea</text>
</docTitle>
<navMap>
  <navPoint id="navPoint1">
    <navLabel>
      <text>One</text>
    </navLabel>
    <content src="../Text/one.xhtml" />
    <navPoint id="navPoint2">
      <navLabel>
        <text>Part Two</text>
      </navLabel>
      <content src="../Text/two%20words.xhtml#part%201" />
    </navPoint>
  </navPoint>
  <navPoint id="navPoint3">
    <navLabel>
      <text>End</text>
    </navLabel>
    <content src="../nav.xhtml#end" />
  </navPoint>
</navMap>
<pageList>
  <pageTarget id="navPoint4" type="normal" value="1">
    <navLabel><text>1</text></navLabel>
    <content src="../Text/one.xhtml#p1" />
  </pageTarget>
</pageList>
</ncx>
'''


class NcxGeneratorLegacyGoldenTest(unittest.TestCase):
    def test_nested_toc_and_page_list(self):
        self.assertEqual(
            generateNCX(NAV, "OEBPS/nav.xhtml", "OEBPS/NCX", "Book &amp; Tea", "urn:book"),
            EXPECTED_NCX,
        )

    def test_landmark_epub_type_mapping(self):
        self.assertEqual(
            generateGuideEntries(NAV, "OEBPS/nav.xhtml", "OEBPS"),
            [
                ("cover", "Text/cover.xhtml", "Cover"),
                ("other.chapter", "Text/one.xhtml", "Chapter"),
                (None, "Text/one.xhtml", "Other"),
            ],
        )

    def test_empty_navigation(self):
        output = generateNCX(
            '<html><body/></html>', "OEBPS/nav.xhtml", "OEBPS", "Title", "id",
        )
        self.assertEqual(
            output,
            '<?xml version="1.0" encoding="utf-8"?>\n'
            '<ncx xmlns="http://www.daisy.org/z3986/2005/ncx/" version="2005-1">\n'
            '  <head>\n'
            '    <meta name="dtb:uid" content="id" />\n'
            '    <meta name="dtb:depth" content="-1" />\n'
            '    <meta name="dtb:totalPageCount" content="0" />\n'
            '    <meta name="dtb:maxPageNumber" content="0" />\n'
            '  </head>\n'
            '<docTitle>\n  <text>Title</text>\n</docTitle>\n'
            '<navMap>\n</navMap>\n</ncx>\n',
        )


if __name__ == "__main__":
    unittest.main()
