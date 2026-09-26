"""Compare the native XmlProcessor with the legacy xmlprocessor / opf_newparser / hrefutils."""

import importlib.util
import json
import random
import subprocess
import sys
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
sys.path[:0] = [
    str(ROOT / "tests"),
    str(ROOT / "src/Resource_Files/python3lib"),
    str(ROOT / "src/Resource_Files/plugin_launchers/python"),
]
import hrefutils
import xmlprocessor
from opf_newparser import Opf_Parser

LEGACY = {name: getattr(xmlprocessor, name) for name in (
    "repairXML", "performOPFSourceUpdates", "performNCXSourceUpdates", "performSMILUpdates",
    "performPageMapUpdates", "anchorNCXUpdates", "anchorNCXUpdatesAfterMerge")}
STATS = {"native": 0, "fallback": 0}


class Probe:
    def __init__(self, program):
        self.process = subprocess.Popen([program], stdin=subprocess.PIPE, stdout=subprocess.PIPE, text=True)

    def call(self, operation, *values):
        self.process.stdin.write(operation + "\t" + "\t".join(v.encode("utf-8", "surrogatepass").hex() for v in values) + "\n")
        self.process.stdin.flush()
        line = self.process.stdout.readline().strip()
        if line == "F":
            return None
        if not line.startswith("S"):
            raise RuntimeError("probe failed: " + line)
        return bytes.fromhex(line[1:]).decode("utf-8")


PROBE = None


def legacy_well_formed(data):
    return xmlprocessor._well_formed(xmlprocessor._remove_xml_header(data))


def compare(name, operation, args, probe_args):
    actual = PROBE.call(operation, *probe_args)
    try:
        expected = LEGACY[name](*args)
    except Exception:
        # The editor surfaces the legacy exception; native must defer so it still does.
        if actual is not None:
            raise AssertionError("%s produced output where legacy raises:\n%s" % (name, args[0]))
        STATS["fallback"] += 1
        return None
    if actual is None:
        STATS["fallback"] += 1
        if legacy_well_formed(args[0]):
            raise AssertionError("%s deferred a document lxml parses strictly:\n%s" % (name, args[0]))
        return expected
    STATS["native"] += 1
    if actual != expected:
        raise AssertionError("%s differs\n--- input\n%s\n--- legacy\n%s\n--- native\n%s" % (name, args[0], expected, actual))
    return expected


def patch():
    def updates(name, operation):
        def run(data, newbkpath, oldbkpath, keys, values):
            mapping = json.dumps(dict(zip(keys, values)), ensure_ascii=False)
            return compare(name, operation, (data, newbkpath, oldbkpath, keys, values), (data, newbkpath, oldbkpath, mapping))
        return run

    xmlprocessor.repairXML = lambda data, mtype="", indent_chars="  ": compare(
        "repairXML", "R", (data, mtype, indent_chars), (data, mtype))
    xmlprocessor.performOPFSourceUpdates = updates("performOPFSourceUpdates", "O")
    xmlprocessor.performNCXSourceUpdates = updates("performNCXSourceUpdates", "N")
    xmlprocessor.performSMILUpdates = updates("performSMILUpdates", "S")
    xmlprocessor.performPageMapUpdates = updates("performPageMapUpdates", "P")
    xmlprocessor.anchorNCXUpdates = lambda data, ncx, origin, keys, values: compare(
        "anchorNCXUpdates", "A", (data, ncx, origin, keys, values),
        (data, ncx, origin, json.dumps(dict(zip(keys, values)), ensure_ascii=False)))
    xmlprocessor.anchorNCXUpdatesAfterMerge = lambda data, ncx, sink, merged: compare(
        "anchorNCXUpdatesAfterMerge", "M", (data, ncx, sink, merged), (data, ncx, sink, json.dumps(merged, ensure_ascii=False)))


NCX = '''<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE ncx PUBLIC "-//NISO//DTD ncx 2005-1//EN"
   "http://www.daisy.org/z3986/2005/ncx-2005-1.dtd">
<ncx xmlns="http://www.daisy.org/z3986/2005/ncx/" version="2005-1" xml:lang="en">
  <head>
    <meta name="dtb:uid" content="urn:uuid:1"/>
    <meta name="dtb:depth" content="2">  </meta>
    <!-- a comment with &amp; and <b> -->
  </head>
  <docTitle><text>A &amp; B &lt;c&gt; &#233; \u00a0 nbsp</text></docTitle>
  <docAuthor><text><![CDATA[x < y & z]]></text></docAuthor>
  <navMap>
    <navPoint id="n1" playOrder="1">
      <navLabel><text>Chapter  1\u3000</text></navLabel>
      <content src="Text/Section%200001.xhtml"/>
      <navPoint id="n1.1" playOrder="2" class='a"b'>
        <navLabel><text>Part &#x41;</text></navLabel>
        <content src="Text/Section%200001.xhtml#sec%201"/>
      </navPoint>
    </navPoint>
    <?sigil keep this?>
    <navPoint id="n2" playOrder="3"><navLabel><text>\u7b2c\u4e8c\u7ae0</text></navLabel><content src="Text/%E7%AB%A0.xhtml#p"></content></navPoint>
    <navPoint id="n3" playOrder="4"><navLabel><text>Web</text></navLabel><content src="http://example.com/a.xhtml#x"/></navPoint>
  </navMap>
  <pageList><pageTarget type="normal" id="p1" value="1"><navLabel><text>1</text></navLabel><content src="Text/a.xhtml#page1"/></pageTarget></pageList>
</ncx>
'''

OPF2 = '''<?xml version="1.0" encoding="utf-8" standalone="yes"?>
<package xmlns="http://www.idpf.org/2007/opf" unique-identifier="BookId" version="2.0">
  <metadata xmlns:mydc="http://purl.org/dc/elements/1.1/" xmlns:opf="http://www.idpf.org/2007/opf">
    <mydc:identifier id="BookId" opf:scheme="UUID">urn:uuid:a418a8f1</mydc:identifier>
    <mydc:title>T &amp; U &lt;v&gt;</mydc:title>
    <mydc:creator opf:role="aut" opf:file-as="Doe, J">J &quot;Doe&quot;</mydc:creator>
    <meta name="cover" content="cover.jpg"/>
    <meta name="Sigil version" content="2.0"></meta>
    <!-- metadata comment -->
  </metadata>
  <manifest>
    <item href="toc.ncx" id="ncx" media-type="application/x-dtbncx+xml" />
    <item href="Text/Section%200001.xhtml" id="s1" media-type="application/xhtml+xml"/>
    <item href="Images/\u5c01\u9762 1.jpg" id="cover.jpg" media-type="image/jpeg" properties="cover-image"/>
    <item href="Styles/a&amp;b.css" id="css" media-type="text/css"/>
  </manifest>
  <spine toc="ncx" page-progression-direction="rtl">
    <itemref idref="s1" linear="yes"/>
  </spine>
  <guide>
    <reference href="Text/Section%200001.xhtml#start" title="Start &amp; go" type="text"/>
  </guide>
</package>
'''

OPF3 = '''<?xml version='1.0' encoding='UTF-8'?>
<opf:package xmlns:opf="http://www.idpf.org/2007/opf" version="3.0" unique-identifier="id" prefix="rendition: http://www.idpf.org/vocab/rendition/#">
<opf:metadata xmlns:dc="http://purl.org/dc/elements/1.1/">
<dc:identifier id="id">urn:x</dc:identifier><dc:title id="t" xml:lang="ja">\u65e5\u672c</dc:title>
<opf:meta refines="#t" property="alternate-script">Nihon</opf:meta>
<opf:meta property="dcterms:modified">2026-01-01T00:00:00Z</opf:meta>
<opf:link rel="record" href="Misc/record.xml" media-type="application/xml"/>
</opf:metadata>
<opf:manifest><opf:item id="nav" href="nav.xhtml" media-type="application/xhtml+xml" properties="nav"/><opf:item id="c1" href="Text/c 1.xhtml" media-type="application/xhtml+xml" media-overlay="s"/><opf:item id="s" href="Audio/c1.smil" media-type="application/smil+xml"/></opf:manifest>
<opf:spine><opf:itemref idref="c1"/></opf:spine>
<opf:bindings><opf:mediaType media-type="application/x-demo" handler="c1"/></opf:bindings>
</opf:package>'''

SMIL = '''<?xml version="1.0" encoding="UTF-8"?>
<smil xmlns="http://www.w3.org/ns/SMIL" xmlns:epub="http://www.idpf.org/2007/ops" version="3.0">
  <body epub:textref="../Text/c%201.xhtml">
    <seq id="s1" epub:textref="../Text/c%201.xhtml#sec" epub:type="chapter">
      <par id="p1"><text src="../Text/c%201.xhtml#w1"/><audio src="../Audio/c1.mp3" clipBegin="0s" clipEnd="1s"/></par>
      <par id="p2"><text src="../Text/c%201.xhtml#w2">  </text><audio src="http://x/y.mp3"/></par>
    </seq>
  </body>
</smil>
'''

PAGEMAP = '''<?xml version="1.0"?>
<page-map xmlns="http://www.idpf.org/2007/opf">
  <page name="i" href="Text/Section%200001.xhtml#pi"/>
  <page name="1" href="../Text/\u7ae0.xhtml"/>
</page-map>
'''


def variants(document):
    yield document
    yield document.replace("\n", "\r\n")
    yield document.replace("\n  ", "\n\t")
    yield xmlprocessor._remove_xml_header(document)
    yield "  " + document
    yield document.replace("<!--", "<!-- -->\n<!--", 1)
    yield document + "<!-- trailing -->"
    yield document.replace(">", "  >", 3)


BROKEN = [NCX.replace("</docTitle>", "", 1), OPF2.replace("<itemref idref=\"s1\" linear=\"yes\"/>", "<itemref idref=\"s1\">"),
          "<ncx><text>&nbsp;</text></ncx>", "<smil><text src='a'></smil>", "<page-map><page href='a'>"]

UPDATES = {
    "OEBPS/Text/Section 0001.xhtml": "OEBPS/Text/Moved/Section 0001.xhtml",
    "OEBPS/Text/\u7ae0.xhtml": "OEBPS/Misc/\u7ae0 2.xhtml",
    "OEBPS/Images/\u5c01\u9762 1.jpg": "OEBPS/Images/cover.jpg",
    "OEBPS/Styles/a&b.css": "OEBPS/Styles/b c.css",
    "OEBPS/Audio/c1.smil": "OEBPS/Audio/Chapter One.smil",
    "OEBPS/Text/c 1.xhtml": "OEBPS/Text/c%1.xhtml",
    "OEBPS/Text/a.xhtml": "OEBPS/b.xhtml",
}


class XmlProcessorParityTest(unittest.TestCase):
    def test_legacy_golden_cases(self):
        spec = importlib.util.spec_from_file_location("xmlprocessor_legacy_golden_test",
                                                      ROOT / "tests/xmlprocessor_legacy_golden_test.py")
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)
        result = unittest.TextTestRunner(verbosity=0).run(unittest.defaultTestLoader.loadTestsFromModule(module))
        self.assertTrue(result.wasSuccessful())

    def test_documents(self):
        keys, values = list(UPDATES), list(UPDATES.values())
        for mtype, base in (("application/x-dtbncx+xml", NCX), ("application/oebps-package+xml", OPF2),
                            ("application/oebps-package+xml", OPF3), ("application/smil+xml", SMIL),
                            ("application/oebps-page-map+xml", PAGEMAP), ("", NCX)):
            for document in list(variants(base)) + BROKEN:
                xmlprocessor.repairXML(document, mtype)
                for new, old in (("OEBPS/content.opf", "OEBPS/content.opf"), ("OEBPS/Misc/content.opf", "OEBPS/content.opf"),
                                 ("OEBPS/toc.ncx", "OEBPS/Text/old.ncx"), ("OEBPS/Audio/c1.smil", "OEBPS/Audio/c1.smil"),
                                 ("content.opf", "OEBPS/content.opf")):
                    xmlprocessor.performOPFSourceUpdates(document, new, old, keys, values)
                    xmlprocessor.performNCXSourceUpdates(document, new, old, keys, values)
                    xmlprocessor.performSMILUpdates(document, new, old, keys, values)
                    xmlprocessor.performPageMapUpdates(document, new, old, keys, values)
                ids = {"sec 1": "OEBPS/Text/Split.xhtml", "page1": "OEBPS/Text/b.xhtml", "p": "OEBPS/Text/\u7ae0.xhtml"}
                xmlprocessor.anchorNCXUpdates(document, "OEBPS/toc.ncx", "OEBPS/Text/Section 0001.xhtml", list(ids), list(ids.values()))
                xmlprocessor.anchorNCXUpdates(document, "OEBPS/toc.ncx", "OEBPS/Text/a.xhtml", list(ids), list(ids.values()))
                xmlprocessor.anchorNCXUpdatesAfterMerge(document, "OEBPS/toc.ncx", "OEBPS/Text/Section 0001.xhtml",
                                                        ["OEBPS/Text/\u7ae0.xhtml", "OEBPS/Text/a.xhtml"])

    def test_random_trees(self):
        generator = random.Random(5)
        names = ["navPoint", "navLabel", "text", "content", "meta", "item", "head", "page", "par", "x:thing", "docTitle"]
        texts = ["", " ", "\n  ", "a &amp; b", "&quot;&apos;", "\r\n", "]]&gt;", "&lt;&gt;", "\u00a0", "&#160;", "&amp;nbsp;", "x\ty", "\u65e5", "&#x41;", "<![CDATA[<&]]>",
                 "<!--c-->", "<!-- -->", "<!---->", "<?pi data?>", "<?pi?>", "  lead", "trail  ", "&amp;amp;"]
        values = ["a", "a b", 'q"uote', "ap'os", "<>&amp;", "\u00a0", "Text/a%20b.xhtml#f", "../x.xhtml", "http://h/p",
                  "\u7ae0.xhtml", "", "&#233;", "a&#10;b\tc", " lead ", "&apos;"]

        def element(depth):
            name = generator.choice(names)
            attrs = "".join(' %s="%s"' % (key, generator.choice(values).replace('"', "&quot;").replace("<", "&lt;"))
                            for key in generator.sample(["src", "href", "id", "class", "xml:lang", "x:hint", "epub:textref"],
                                                        generator.randrange(0, 3)))
            if depth > 3 or generator.random() < 0.3:
                return "<%s%s/>" % (name, attrs) if generator.random() < 0.5 else "<%s%s>%s</%s>" % (
                    name, attrs, generator.choice(texts), name)
            children = "".join(generator.choice([element(depth + 1), generator.choice(texts)])
                               for _ in range(generator.randrange(0, 4)))
            return "<%s%s>%s</%s>" % (name, attrs, children, name)

        roots = ['<ncx xmlns="http://www.daisy.org/z3986/2005/ncx/" xmlns:x="urn:x" xmlns:epub="http://www.idpf.org/2007/ops">',
                 '<package xmlns="http://www.idpf.org/2007/opf" xmlns:x="urn:x" xmlns:epub="http://www.idpf.org/2007/ops">',
                 '<smil xmlns:x="urn:x" xmlns:epub="http://www.idpf.org/2007/ops">']
        keys, updates = list(UPDATES), list(UPDATES.values())
        for _ in range(400):
            root = generator.choice(roots)
            name = root[1:root.index(" ")]
            document = '<?xml version="1.0"?>\n' + root + "".join(element(1) for _ in range(generator.randrange(1, 5))) + "</%s>" % name
            for mtype in ("application/x-dtbncx+xml", "application/smil+xml", "application/oebps-page-map+xml"):
                xmlprocessor.repairXML(document, mtype)
            xmlprocessor.performNCXSourceUpdates(document, "OEBPS/toc.ncx", "OEBPS/Text/x.ncx", keys, updates)
            xmlprocessor.performSMILUpdates(document, "OEBPS/Audio/a.smil", "OEBPS/a.smil", keys, updates)
            xmlprocessor.performPageMapUpdates(document, "OEBPS/page-map.xml", "OEBPS/Text/page-map.xml", keys, updates)
            xmlprocessor.performOPFSourceUpdates(document, "OEBPS/content.opf", "OEBPS/Text/content.opf", keys, updates)

    def test_opf_rebuild(self):
        for document in list(variants(OPF2)) + list(variants(OPF3)) + [OPF2.replace("opf:", "OPF:"),
                                                                       OPF2.replace("<guide>", "<guide/><x>").replace("</guide>", "</x>")]:
            self.assertEqual(PROBE.call("X", document), Opf_Parser(document).rebuild_opfxml())

    def test_hrefutils(self):
        generator = random.Random(6)
        pieces = ["a", "B", "%", "%2", "%20", "%zz", "%E7%AB%A0", "%E7%AB", "%C3", "%FF", "%ed%a0%80", "%F0%9F%98%80", "%f4%90%80%80",
                  "\u7ae0", "\U0001f600", " ", "#", "/", "..", ".", "~", "_", "\u00a0", "\ufffe", "\uf8ff", "\U000e0001", "?", "&"]
        paths = ["", "a", "OEBPS/Text/a.xhtml", "OEBPS/Text/", "/abs/x", "OEBPS//Text/a", "../x", "a/../../b", "./a/./b", "OEBPS"]
        strings = {"".join(generator.choice(pieces) for _ in range(generator.randrange(0, 6))) for _ in range(20000)}
        for value in sorted(strings):
            self.assertEqual(PROBE.call("E", value), hrefutils.urlencodepart(value), value)
            self.assertEqual(PROBE.call("D", value), hrefutils.urldecodepart(value), value)
        for first in paths:
            self.assertEqual(PROBE.call("T", first), hrefutils.startingDir(first))
            for second in paths:
                self.assertEqual(PROBE.call("B", first, second), hrefutils.buildBookPath(first, second), (first, second))
                self.assertEqual(PROBE.call("L", first, second), hrefutils.buildRelativePath(first, second), (first, second))


if __name__ == "__main__":
    PROBE = Probe(sys.argv[1])
    patch()
    program = unittest.main(argv=[sys.argv[0]], exit=False)
    print("xml_processor_parity: %(native)d native, %(fallback)d deferred to the recovering parser" % STATS)
    sys.exit(0 if program.result.wasSuccessful() else 1)
