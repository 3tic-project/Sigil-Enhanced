"""Compare the C++ plugin package update with the legacy opf_package_update.

The legacy routine receives the JSON text the application produced with
QJsonDocument, so both sides see the same attribute order.
"""

import importlib.util
import json
import subprocess
import sys
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
sys.path[:0] = [
    str(ROOT / "tests/fixtures"),
    str(ROOT / "src/Resource_Files/plugin_launchers/python"),
]
import opf_package_update_legacy as opf_package_update
from lxml import etree


OPF = "http://www.idpf.org/2007/opf"
DC = "http://purl.org/dc/elements/1.1/"
LEGACY = opf_package_update.apply_update


class Probe:
    def __init__(self, program):
        self.process = subprocess.Popen([program], stdin=subprocess.PIPE, stdout=subprocess.PIPE, text=True)

    def call(self, operation, *values):
        payload = operation + "\t" + "\t".join(value.encode().hex() for value in values) + "\n"
        self.process.stdin.write(payload)
        self.process.stdin.flush()
        line = self.process.stdout.readline().strip()
        if line == "E":
            raise ValueError("native package update failed")
        if not line.startswith("S"):
            raise RuntimeError("malformed native response: " + line)
        return bytes.fromhex(line[1:]).decode()


class Mismatch(AssertionError):
    pass


def compare(probe, source, operation, payload_json):
    canonical = probe.call("J", payload_json)
    try:
        expected = LEGACY(source, operation, canonical)
        legacy_error = None
    except Exception as error:  # the legacy routine also fails on lxml and parser errors
        expected, legacy_error = None, error
    try:
        actual = probe.call("P", source, operation, payload_json)
    except ValueError:
        actual = None
    if legacy_error is None and actual is None:
        raise Mismatch("native update rejected input the legacy routine accepts:\n" + operation + " " + canonical)
    if legacy_error is not None and actual is not None:
        raise Mismatch("native update accepted input the legacy routine rejects (" + repr(legacy_error) + "):\n"
                       + operation + " " + canonical)
    if legacy_error is not None:
        raise ValueError(str(legacy_error))
    if actual != expected:
        raise Mismatch("native update differs from the legacy routine:\n" + operation + " " + canonical
                       + "\n--- legacy\n" + expected + "\n--- native\n" + actual)
    return expected


SOURCES = {
    "epub3": """<?xml version="1.0" encoding="utf-8"?>
<package version="3.0" unique-identifier="BookId" xmlns="http://www.idpf.org/2007/opf" prefix="rendition: http://www.idpf.org/vocab/rendition/#">
  <metadata xmlns:dc="http://purl.org/dc/elements/1.1/" xmlns:opf="http://www.idpf.org/2007/opf">
    <dc:identifier id="BookId">urn:uuid:1234</dc:identifier>
    <dc:title id="t1" xml:lang="ja">\u65e5\u672c\u8a9e</dc:title>
    <meta refines="#t1" property="alternate-script" xml:lang="en">Nihongo</meta>
    <dc:language>ja</dc:language>
    <meta property="dcterms:modified">2026-01-01T00:00:00Z</meta>
    <meta name="cover" content="cover-image"/>
    <meta property="rendition:layout">pre-paginated</meta>
  </metadata>
  <manifest>
    <item id="nav" href="nav.xhtml" media-type="application/xhtml+xml" properties="nav"/>
    <item id="cover-image" href="Images/cover.jpg" media-type="image/jpeg" properties="cover-image"/>
    <item id="c1" href="Text/c1.xhtml" media-type="application/xhtml+xml" media-overlay="s1"/>
    <item id="s1" href="Audio/c1.smil" media-type="application/smil+xml"/>
    <item id="c2" href="Text/%E7%AB%A0.xhtml" media-type="application/xhtml+xml" fallback="c1"/>
  </manifest>
  <spine page-progression-direction="rtl">
    <itemref idref="c1" properties="page-spread-right"/>
    <itemref idref="c2" linear="no"/>
    <itemref idref="c2"/>
  </spine>
</package>
""",
    "epub2": """<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<package xmlns="http://www.idpf.org/2007/opf" unique-identifier="uuid_id" version="2.0">
\t<metadata xmlns:opf="http://www.idpf.org/2007/opf" xmlns:dc="http://purl.org/dc/elements/1.1/" xmlns:calibre="http://calibre.kovidgoyal.net/2009/metadata">
\t\t<dc:title>A &amp; B &lt;tag&gt;</dc:title>
\t\t<dc:creator opf:role="aut" opf:file-as="Doe, Jane">Jane Doe</dc:creator>
\t\t<dc:identifier id="uuid_id" opf:scheme="uuid">1234</dc:identifier>
\t\t<dc:date opf:event="modification">2020-01-01</dc:date>
\t\t<calibre:series>Series</calibre:series>
\t\t<meta name="calibre:series_index" content="1"/>
\t</metadata>
\t<manifest>
\t\t<item href="toc.ncx" id="ncx" media-type="application/x-dtbncx+xml"/>
\t\t<item href="Text/a.xhtml" id="a" media-type="application/xhtml+xml"/>
\t</manifest>
\t<spine toc="ncx">
\t\t<itemref idref="a"/>
\t</spine>
\t<guide>
\t\t<reference type="text" href="Text/a.xhtml" title="Start"/>
\t</guide>
</package>
""".replace("\n", "\r\n"),
    "prefixes": """<?xml version='1.0' encoding='UTF-8'?>
<opf:package xmlns:opf='http://www.idpf.org/2007/opf' xmlns:books='http://purl.org/dc/elements/1.1/' xmlns:e='urn:ext' version='3.0'>
  <opf:metadata><books:title>T</books:title><opf:meta property='e:x' e:attr='1'>v</opf:meta><!-- keep --></opf:metadata>
  <opf:manifest/>
  <opf:spine/>
</opf:package>
""",
}


def model_entries(source):
    """Rebuild the metadata items a plugin would send back unchanged."""
    root = etree.fromstring(opf_package_update.model_xml(source).encode())
    reverse = {}
    for prefix, uri in root.nsmap.items():
        reverse.setdefault(uri, prefix)
    reverse[DC] = "dc"
    reverse["http://www.w3.org/XML/1998/namespace"] = "xml"

    def qualified(name, attribute):
        qname = etree.QName(name)
        if qname.namespace is None:
            return qname.localname
        prefix = "opf" if attribute and qname.namespace == OPF else reverse.get(qname.namespace)
        return qname.localname if not prefix else prefix + ":" + qname.localname

    metadata = root.find("{%s}metadata" % OPF)
    return [dict(name=qualified(node.tag, False), content=node.text or "",
                 attributes={qualified(key, True): value for key, value in node.attrib.items()})
            for node in metadata]


def spine_items(source):
    root = etree.fromstring(opf_package_update.model_xml(source).encode())
    return [dict(node.attrib) for node in root.find("{%s}spine" % OPF)]


def metadata_cases(source):
    base = model_entries(source)
    yield dict(items=base)
    yield dict(items=list(reversed(base)))
    yield dict(items=base[1:])
    yield dict(items=[])
    for index in range(len(base)):
        changed = [dict(entry) for entry in base]
        changed[index] = dict(changed[index], content=changed[index]["content"] + " \u00e9 & <x> \"q\" \t\r\n\U00020bb7")
        yield dict(items=changed)
        attributes = dict(changed[index]["attributes"], id="x-%d" % index)
        yield dict(items=base[:index] + [dict(base[index], attributes=attributes)] + base[index + 1:])
    extra = [
        dict(name="dc:subject", content="", attributes={}),
        dict(name="meta", content="v", attributes={"property": "p", "refines": "#t1", "xml:lang": "fr"}),
        dict(name="opf:meta", content="v", attributes={"opf:scheme": "s"}),
        dict(name="custom:date", content="2026", attributes={"xmlns:custom": "urn:date", "custom:event": "e"}),
        dict(name="dc:date", content="2024", attributes={"opf:event": "modification", "xmlns:opf": OPF}),
        dict(name="meta", content="v", attributes={"xmlns": "urn:other", "a": "1"}),
        dict(name="x", content="v", attributes={"xmlns": "urn:d", "xmlns:q": "urn:q", "q:w": "v\t\n\"'<>&"}),
        dict(name="dc:title", content="v", attributes={"xmlns:dc": "urn:not-dc"}),
        dict(name="meta", content="v", attributes={"xmlns:opf": "urn:not-opf", "opf:a": "1"}),
        dict(name="alt:title", content="v", attributes={"xmlns:alt": DC, "alt:x": "1"}),
        dict(name="xml:thing", content="v", attributes={}),
        dict(name="meta", content="v", attributes={"xmlns:xml": "http://www.w3.org/XML/1998/namespace"}),
        dict(name="meta", content="v", attributes={"e2:a": "1", "xmlns:e2": "urn:ext"}),
        dict(name="meta", content="v", attributes={"xmlns:u": "http://[::1]/p?q#f", "u:a": "1"}),
        dict(name="meta", content="v", attributes={"xmlns:u": "x:y:z", "u:a": "1"}),
        dict(name="meta", content="v", attributes={"xmlns:u": "#", "u:a": "1"}),
        dict(name="meta", content="v", attributes={"xmlns:u": "//host/p", "u:a": "1"}),
        dict(name="meta", content="v", attributes={"xmlns:u": "http://x/%41", "u:a": "1"}),
        dict(name="meta", content="v", attributes={"xmlns:u": "mailto:a@b.c", "u:a": "1"}),
        dict(name="meta", content="v", attributes={"xmlns:u": "http://u:p@h:80/a", "u:a": "1"}),
        dict(name="\u00e9l\u00b7", content="v", attributes={"\u4e00": "1"}),
    ]
    for entry in extra:
        yield dict(items=base + [entry])
    invalid = [
        dict(name="dc:title"),
        dict(name="dc:title", content=1),
        dict(name="dc:title", content="x", attributes=None),
        dict(name="dc:title", content="x", attributes=[]),
        dict(name="dc:title", content="x", attributes={"id": 4}),
        dict(name="", content="x"),
        dict(name="unknown:title", content="x"),
        dict(name="bad:name:again", content="x"),
        dict(name=":x", content="x"),
        dict(name="x:", content="x"),
        dict(name="1abc", content="x"),
        dict(name="\u00b7a", content="x"),
        dict(name="a b", content="x"),
        dict(name="meta", content="\x01"),
        dict(name="meta", content="\ufffe"),
        dict(name="meta", content="a\ud800b"),
        dict(name="meta", content="x", attributes={"a": "\x0b"}),
        dict(name="meta", content="x", attributes={"1a": "v"}),
        dict(name="meta", content="x", attributes={"u:a": "v"}),
        dict(name="meta", content="x", attributes={"xmlns:u": ""}),
        dict(name="meta", content="x", attributes={"xmlns:": "urn:x"}),
        dict(name="meta", content="x", attributes={"xmlns:xmlns": "urn:x"}),
        dict(name="meta", content="x", attributes={"xmlns:xml": "urn:x"}),
        dict(name="meta", content="x", attributes={"xmlns:x2": "http://www.w3.org/XML/1998/namespace"}),
        dict(name="meta", content="x", attributes={"xmlns": "http://www.w3.org/XML/1998/namespace"}),
        dict(name="meta", content="x", attributes={"xmlns:1u": "urn:x"}),
    ] + [dict(name="meta", content="x", attributes={"xmlns:u": uri}) for uri in [
        "urn:\u00e9", "urn:a b", "urn:[x]", "http://x/%zz", "urn:a|b", "urn:a\"b", "\\\\x", "urn:a^b",
        "http://x/a`b", ":x", "http://ho st/", "urn:<a>", "http://x/{a}", "http://[a/", "a#b#c"]]
    for entry in invalid:
        yield dict(items=base + [entry])
    yield dict(items="nope")
    yield dict()
    yield dict(items=[7])


def manifest_cases(source):
    root = etree.fromstring(opf_package_update.model_xml(source).encode())
    items = [node.attrib for node in root.find("{%s}manifest" % OPF)]
    hrefs = [item["href"] for item in items]
    ids = [item["id"] for item in items]
    yield dict()
    yield dict(removals=[], relocations=[], additions=[])
    for href in hrefs:
        yield dict(removals=[href])
        yield dict(removals=[href, "missing.xhtml", href])
        yield dict(relocations=[dict(original_href=href, target_href="Moved/" + href)])
        yield dict(relocations=[dict(original_href=href, target_href=href)])
        yield dict(removals=[href], relocations=[dict(original_href=href, target_href="x.xhtml")])
    if len(hrefs) > 1:
        yield dict(relocations=[dict(original_href=hrefs[0], target_href=hrefs[1])])
        yield dict(relocations=[dict(original_href=hrefs[0], target_href="t.xhtml"),
                                dict(original_href="t.xhtml", target_href=hrefs[0])])
        yield dict(removals=[hrefs[1]], relocations=[dict(original_href=hrefs[0], target_href=hrefs[1])])
    new = [
        {"id": "n1", "href": "Text/z.xhtml", "media-type": "application/xhtml+xml", "properties": "scripted svg"},
        {"id": "n2", "href": "Text/\U00020bb7.xhtml", "media-type": "application/xhtml+xml"},
        {"id": "n3", "href": "Text/\uffe0.xhtml", "media-type": "application/xhtml+xml", "fallback": "n1", "media-overlay": ""},
        {"id": "n4", "href": "Text/a b&c\".xhtml", "media-type": "text/css", "properties": ""},
    ]
    yield dict(additions=new)
    yield dict(additions=list(reversed(new)), removals=hrefs[:1])
    for identifier, href in zip(ids, hrefs):
        yield dict(additions=[{"id": identifier, "href": href, "media-type": "application/x-changed", "properties": "p"}])
        yield dict(additions=[{"id": identifier, "href": href, "media-type": "text/plain", "properties": "",
                               "fallback": "", "media-overlay": ""}])
        yield dict(additions=[{"id": identifier, "href": "other.xhtml", "media-type": "text/plain"}])
        yield dict(additions=[{"id": "fresh", "href": href, "media-type": "text/plain"}])
        yield dict(relocations=[dict(original_href=href, target_href="r.xhtml")],
                   additions=[{"id": identifier, "href": "r.xhtml", "media-type": "text/plain"}])
    for invalid in [dict(removals=[7]), dict(removals="a"), dict(relocations=[7]), dict(relocations=None),
                    dict(relocations=[dict(original_href="", target_href="x")]),
                    dict(additions=[{"id": "c", "href": "c.xhtml"}]), dict(additions=[7]),
                    dict(additions=[{"id": "c", "href": "c.xhtml", "media-type": "t", "properties": 5}]),
                    dict(additions=[{"id": "c", "href": "c\x01.xhtml", "media-type": "t"}]),
                    dict(additions=[{"id": "c", "href": "c.xhtml", "media-type": "t"},
                                    {"id": "d", "href": "c.xhtml", "media-type": "t"}])]:
        yield invalid


def spine_cases(source):
    items = spine_items(source)
    yield dict(items=items, attributes={})
    yield dict(items=items)
    yield dict(items=list(reversed(items)))
    yield dict(items=[])
    yield dict(items=items + items)
    yield dict(items=[dict(idref=item["idref"]) for item in items])
    yield dict(items=items + [dict(idref="new", id="i", linear="no", properties="p")])
    yield dict(items=[dict(item, linear="no", properties="x y", id="z") for item in items])
    yield dict(items=[dict(item, linear="", properties="", id="") for item in items])
    for attributes in [{"page-progression-direction": "ltr"}, {"toc": "ncx"}, {"toc": ""},
                       {"xml:lang": "en"}, {"opf:x": "1"}, {"dc:x": "1"},
                       {"xmlns:k": "urn:k", "k:a": "1"}, {"xmlns:k": "urn:k", "k:a": "1", "xmlns:m": "urn:m", "m:b": "2"},
                       {"xmlns:k": "http://www.w3.org/1999/xhtml", "k:a": "1"}, {"xmlns:k": DC, "k:a": "1"},
                       {"xmlns:k": "urn:k b", "k:a": "1"}, {"xmlns:": "urn:empty", "a": "1"},
                       {"xmlns": "urn:default", "a": "1"}, {"xmlns:ns0": "urn:z", "ns0:a": "1", "xmlns:k": "urn:k", "k:b": "2"},
                       {"xmlns:e": "urn:ext", "e:a": "1"}]:
        yield dict(items=items, attributes=attributes)
    for invalid in [dict(items=[dict(idref=7)]), dict(items=[dict(idref="")]), dict(items=[7]),
                    dict(items=[dict(idref="a", linear=7)]), dict(items=[dict(idref="a", id=None)]),
                    dict(items=[], attributes={"toc": 7}), dict(items=[], attributes=[]),
                    dict(items=None), dict(), dict(items=[], attributes={"u:a": "1"}),
                    dict(items=[], attributes={"1a": "1"}), dict(items=[], attributes={"a": "\x02"}),
                    dict(items=[], attributes={"xmlns:k": ""})]:
        yield invalid


def corpus(probe):
    count = failures = accepted = 0
    for name, source in SOURCES.items():
        for operation, cases in (("metadata", metadata_cases), ("manifest", manifest_cases), ("spine", spine_cases)):
            for payload in cases(source):
                count += 1
                try:
                    compare(probe, source, operation, json.dumps(payload))
                    accepted += 1
                except ValueError:
                    pass
                except Mismatch as error:
                    failures += 1
                    print("[" + name + "] " + str(error), file=sys.stderr)
    for operation in ("unknown", "Metadata"):
        count += 1
        try:
            compare(probe, SOURCES["epub3"], operation, "{}")
        except ValueError:
            pass
    for broken in ("<package>", "<package xmlns='urn:x'/>", SOURCES["epub3"].replace("</manifest>", "</manifest><manifest/>")):
        for operation in ("metadata", "manifest", "spine"):
            count += 1
            try:
                compare(probe, broken, operation, json.dumps(dict(items=[])))
            except ValueError:
                pass
    print("opf_package_update_parity: %d corpus cases (%d accepted), %d mismatches" % (count, accepted, failures))
    return failures == 0


def main():
    probe = Probe(sys.argv[1])
    spec = importlib.util.spec_from_file_location("opf_package_update_test", ROOT / "tests/opf_package_update_test.py")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    module.apply_update = lambda source, operation, payload: compare(probe, source, operation, payload)
    result = unittest.TextTestRunner(verbosity=1).run(unittest.defaultTestLoader.loadTestsFromModule(module))
    corpus_ok = corpus(probe)
    probe.process.stdin.close()
    probe.process.wait()
    if not result.wasSuccessful() or not corpus_ok:
        raise SystemExit(1)


if __name__ == "__main__":
    main()
