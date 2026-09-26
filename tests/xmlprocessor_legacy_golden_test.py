"""Freeze editor XML behavior before replacing xmlprocessor with C++."""

import importlib.util
import sys
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
sys.path[:0] = [
    str(ROOT / "src/Resource_Files/python3lib"),
    str(ROOT / "src/Resource_Files/plugin_launchers/python"),
]

if importlib.util.find_spec("lxml") is None:
    # The application bundles lxml, but a build host may lack it.
    print("xmlprocessor golden test unavailable: lxml is not installed", file=sys.stderr)
    sys.exit(77)
import xmlprocessor as legacy


DECL = '<?xml version="1.0" encoding="utf-8" ?>\n'
PACKAGE_TYPE = "application/oebps-package+xml"
NCX_TYPE = "application/x-dtbncx+xml"
SMIL_TYPE = "application/smil+xml"
PAGE_TYPE = "application/oebps-page-map+xml"


class XmlProcessorLegacyGoldenTest(unittest.TestCase):
    def test_well_formed_documents_keep_source_except_opf(self):
        source = (
            '<?xml version="1.0"?>\r\n'
            '<ncx><navMap><!-- keep --><content src="Text/a.xhtml#id"/>'
            '</navMap></ncx>'
        )
        for media_type, data in (
            (NCX_TYPE, source),
            (SMIL_TYPE, '<smil><text src="a.xhtml#id"/></smil>'),
            (PAGE_TYPE, '<page-map><page href="a.xhtml#id"/></page-map>'),
        ):
            with self.subTest(media_type=media_type):
                self.assertEqual(
                    legacy.WellFormedXMLErrorCheck(data, media_type),
                    ["-1", "-1", "well-formed"],
                )
                self.assertTrue(legacy.IsWellFormedXML(data, media_type))
                self.assertEqual(legacy.repairXML(data, media_type), data)

        package = (
            "<?xml version='1.0'?><package "
            "xmlns='http://www.idpf.org/2007/opf' version='3.0'>"
            "<metadata/><manifest><item id='a' href='a.xhtml' "
            "media-type='application/xhtml+xml'/></manifest>"
            "<spine><itemref idref='a'/></spine></package>"
        )
        self.assertTrue(legacy.IsWellFormedXML(package, PACKAGE_TYPE))
        self.assertEqual(
            legacy.repairXML(package, PACKAGE_TYPE),
            '<?xml version="1.0" encoding="utf-8"?>\n'
            '<package version="3.0" unique-identifier="bookid" '
            'xmlns="http://www.idpf.org/2007/opf">\n'
            '  <metadata>\n  </metadata>\n'
            '  <manifest>\n'
            '    <item id="a" href="a.xhtml" media-type="application/xhtml+xml"/>\n'
            '  </manifest>\n'
            '  <spine>\n    <itemref idref="a"/>\n  </spine>\n'
            '</package>\n',
        )

    def test_broken_documents_report_first_error_and_repair(self):
        cases = (
            (
                PACKAGE_TYPE,
                '<package><metadata></package>',
                ["1", "30", "Opening and ending tag mismatch: metadata line 1 and package"],
                '<?xml version="1.0" encoding="utf-8"?>\n'
                '<package version="2.0" unique-identifier="bookid">\n'
                '  <metadata>\n  </metadata>\n'
                '  <manifest>\n  </manifest>\n'
                '  <spine>\n  </spine>\n</package>\n',
            ),
            (
                NCX_TYPE,
                '<ncx><navMap><navPoint><content src="a"/></navMap></ncx>',
                ["1", "51", "Opening and ending tag mismatch: navPoint line 1 and navMap"],
                DECL + '<ncx>\n  <navMap>\n    <navPoint>\n'
                '      <content src="a"/>\n    </navPoint>\n  </navMap>\n</ncx>',
            ),
            (
                SMIL_TYPE,
                '<smil><body><seq><par><text src="a"/></seq></body></smil>',
                ["1", "44", "Opening and ending tag mismatch: par line 1 and seq"],
                DECL + '<smil><body><seq><par><text src="a"/>\n'
                '</par></seq></body></smil>',
            ),
            (
                PAGE_TYPE,
                '<page-map><page href="a"></page-map>',
                ["1", "37", "Opening and ending tag mismatch: page line 1 and page-map"],
                DECL + '<page-map><page href="a"/>\n</page-map>',
            ),
        )
        for media_type, source, error, repaired in cases:
            with self.subTest(media_type=media_type):
                self.assertEqual(legacy.WellFormedXMLErrorCheck(source, media_type), error)
                self.assertFalse(legacy.IsWellFormedXML(source, media_type))
                self.assertEqual(legacy.repairXML(source, media_type), repaired)

    def test_source_updates_rebase_paths_and_keep_external_urls(self):
        original = "Text/a%20b.xhtml#frag%20ment"
        updates = ["OEBPS/Text/a b.xhtml"]
        destinations = ["OEBPS/Text/new.xhtml"]
        cases = (
            (
                legacy.performOPFSourceUpdates,
                '<package><item href="' + original + '"/>'
                '<link href="https://example.org/x"/>'
                '<reference href="Text/keep.xhtml"/></package>',
                ("OEBPS/Book/content.opf", "OEBPS/content.opf", updates, destinations),
                DECL + '<package>\n'
                '  <item href="../Text/new.xhtml#frag%20ment"/>\n'
                '  <link href="https://example.org/x"></link>\n'
                '  <reference href="../Text/keep.xhtml"/>\n</package>',
            ),
            (
                legacy.performNCXSourceUpdates,
                '<ncx><content src="' + original + '"/>'
                '<content src="https://example.org/x"/>'
                '<content src="Text/keep.xhtml"/></ncx>',
                ("OEBPS/Nav/toc.ncx", "OEBPS/toc.ncx", updates, destinations),
                DECL + '<ncx>\n'
                '  <content src="../Text/new.xhtml#frag%20ment"/>\n'
                '  <content src="https://example.org/x"/>\n'
                '  <content src="../Text/keep.xhtml"/>\n</ncx>',
            ),
            (
                legacy.performSMILUpdates,
                '<smil xmlns:epub="http://www.idpf.org/2007/ops">'
                '<body epub:textref="../' + original + '">'
                '<text src="../' + original + '"/>'
                '<audio src="https://example.org/a.mp3"/></body></smil>',
                ("OEBPS/Misc/part.smil", "OEBPS/Misc/part.smil", updates, destinations),
                DECL + '<smil xmlns:epub="http://www.idpf.org/2007/ops">'
                '<body epub:textref="../Text/new.xhtml#frag%20ment">'
                '<text src="../Text/new.xhtml#frag%20ment"/>\n'
                '<audio src="https://example.org/a.mp3"/>\n</body></smil>',
            ),
            (
                legacy.performPageMapUpdates,
                '<page-map><page href="../' + original + '"/>'
                '<page href="https://example.org/2"/></page-map>',
                ("OEBPS/Misc/page-map.xml", "OEBPS/Misc/page-map.xml", updates, destinations),
                DECL + '<page-map><page href="../Text/new.xhtml#frag%20ment"/>\n'
                '<page href="https://example.org/2"/>\n</page-map>',
            ),
        )
        for function, source, args, expected in cases:
            with self.subTest(function=function.__name__):
                self.assertEqual(function(source, *args), expected)

    def test_ncx_anchor_split_and_merge(self):
        split = (
            '<ncx><content src="Text/a.xhtml#jump"/>'
            '<content src="Text/a.xhtml#stay"/>'
            '<content src="https://example.org/a.xhtml#jump"/></ncx>'
        )
        self.assertEqual(
            legacy.anchorNCXUpdates(
                split, "OEBPS/toc.ncx", "OEBPS/Text/a.xhtml",
                ["jump"], ["OEBPS/Text/new.xhtml"],
            ),
            DECL + '<ncx>\n'
            '  <content src="Text/new.xhtml#jump"/>\n'
            '  <content src="Text/a.xhtml#stay"/>\n'
            '  <content src="https://example.org/a.xhtml#jump"/>\n</ncx>',
        )
        merged = (
            '<ncx><content src="Text/old.xhtml#frag%20ment"/>'
            '<content src="Text/keep.xhtml"/></ncx>'
        )
        self.assertEqual(
            legacy.anchorNCXUpdatesAfterMerge(
                merged, "OEBPS/toc.ncx", "OEBPS/Text/sink.xhtml",
                ["OEBPS/Text/old.xhtml"],
            ),
            DECL + '<ncx>\n'
            '  <content src="Text/sink.xhtml#frag%2520ment"/>\n'
            '  <content src="Text/keep.xhtml"/>\n</ncx>',
        )


if __name__ == "__main__":
    unittest.main()
