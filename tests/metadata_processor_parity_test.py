"""Compare the native MetadataProcessor with the legacy metaproc2 / metaproc3 modules.

The golden file freezes the legacy output for curated EPUB 2 and EPUB 3
packages (P0-3); the remaining cases compare both implementations live.
Pass --write-golden to regenerate the golden file from the legacy modules.
"""

import json
import random
import subprocess
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
GOLDEN = ROOT / "tests/fixtures/metadata_processor_golden.json"
sys.path[:0] = [str(ROOT / "tests/fixtures"), str(ROOT / "src/Resource_Files/plugin_launchers/python")]
import metaproc2_legacy as metaproc2
import metaproc3_legacy as metaproc3

RS, US = "\x1e", "\x1f"
STATS = {"extract": 0, "apply": 0, "failed": 0}


class Probe:
    def __init__(self, program):
        self.process = subprocess.Popen([program], stdin=subprocess.PIPE, stdout=subprocess.PIPE, text=True)

    def call(self, operation, *values):
        self.process.stdin.write(operation + "\t" + "\t".join(v.encode("utf-8").hex() for v in values) + "\n")
        self.process.stdin.flush()
        line = self.process.stdout.readline().strip()
        if line == "F":
            return None
        if not line.startswith("S"):
            raise RuntimeError("probe failed: " + line)
        return bytes.fromhex(line[1:]).decode("utf-8")


def legacy_module(version):
    return metaproc3 if version.startswith("3") else metaproc2


def legacy_extract(opf, version):
    mdp = legacy_module(version).process_metadata(opf)
    if mdp is None:
        return None
    return {"data": mdp.get_recognized_metadata(), "other": mdp.get_other_meta_xml(),
            "ids": list(mdp.get_id_list()), "tag": mdp.get_metadata_tag()}


def legacy_apply(pieces, opf, version):
    try:
        return legacy_module(version).set_new_metadata(pieces["data"], pieces["other"], list(pieces["ids"]),
                                                       pieces["tag"], opf)
    except Exception:
        return None


def native_extract(probe, opf, version):
    result = probe.call("X", version, opf)
    return None if result is None else json.loads(result)


def native_apply(probe, pieces, opf, version):
    return probe.call("A", version, pieces["data"], pieces["other"], json.dumps(pieces["ids"]), pieces["tag"], opf)


EPUB2 = """<?xml version="1.0" encoding="UTF-8"?>
<package xmlns="http://www.idpf.org/2007/opf" unique-identifier="BookId" version="2.0">
  <metadata xmlns:dc="http://purl.org/dc/elements/1.1/" xmlns:opf="http://www.idpf.org/2007/opf">
    <dc:identifier id="BookId" opf:scheme="UUID">urn:uuid:9c5a</dc:identifier>
    <dc:identifier opf:scheme="ISBN" id="isbn">978-0-00</dc:identifier>
    <dc:title>The &amp; Title &lt;One&gt;</dc:title>
    <dc:creator opf:role="aut" opf:file-as="Doe, Jane">Jane Doe</dc:creator>
    <dc:contributor opf:role="edt">Ed &quot;Itor&quot;</dc:contributor>
    <dc:language>EN-us</dc:language>
    <dc:date opf:event="publication">2020-01-01</dc:date>
    <dc:subject>Fiction</dc:subject>
    <dc:subject id="sub">Drama</dc:subject>
    <dc:description>Line one
line two  </dc:description>
    <dc:rights xmlns:dc="http://purl.org/dc/elements/1.1/">CC</dc:rights>
    <!-- a comment -->
    <meta name="cover" content="cover-image" />
    <meta name="calibre:series" content="Saga &amp; Co" id="series"/>
    <meta content="3" name="calibre:series_index"/>
    <dc:publisher></dc:publisher>
    <dc:source> </dc:source>
  </metadata>
  <manifest>
    <item id="ncx" href="toc.ncx" media-type="application/x-dtbncx+xml"/>
    <item href="Text/a.xhtml" media-type="application/xhtml+xml"/>
  </manifest>
  <spine toc="ncx" id="spine1"><itemref idref="ncx" id="ir1"/></spine>
  <guide><reference type="cover" href="Text/a.xhtml" title="Cover" id="g1"/></guide>
</package>
"""

EPUB3 = """<?xml version="1.0" encoding="utf-8"?>
<package xmlns="http://www.idpf.org/2007/opf" version="3.0" unique-identifier="bookid" prefix="marc: http://id.loc.gov/vocabulary/">
  <metadata xmlns:dc="http://purl.org/dc/elements/1.1/">
    <dc:identifier id="bookid">urn:asin:B00A2HJ45I</dc:identifier>
    <dc:identifier id="isbn">9780000000000</dc:identifier>
    <meta refines="#isbn" property="identifier-type" scheme="onix:codelist5">15</meta>
    <dc:title id="title">\u305f\u3068\u3048\u3070 (Vol. 1)</dc:title>
    <meta refines="#title" property="alternate-script" xml:lang="ja-Latn">Tatoeba</meta>
    <meta refines="#title" property="title-type">main</meta>
    <meta refines="#title" property="file-as">Tatoeba</meta>
    <dc:creator id="creator0">\u767d\u57ce \u308b\u305f</dc:creator>
    <meta refines="#creator0" property="role" scheme="marc:relators">aut</meta>
    <meta refines="#creator0" property="alternate-script">\u30b7\u30e9\u30ad</meta>
    <meta refines="#creator0" property="display-seq" id="seq">1</meta>
    <dc:creator>Second Author</dc:creator>
    <dc:language>zh-hans-cn</dc:language>
    <dc:language>JA</dc:language>
    <dc:publisher>\u89d2\u5ddd\u66f8\u5e97</dc:publisher>
    <dc:date>2008-04-01</dc:date>
    <meta property="dcterms:modified">2026-08-17T14:09:55Z</meta>
    <meta property="belongs-to-collection" id="c01">The Series</meta>
    <meta refines="#c01" property="collection-type">series</meta>
    <meta refines="#c01" property="group-position">2</meta>
    <meta property="schema:accessMode">textual</meta>
    <meta property="rendition:layout">pre-paginated</meta>
    <meta refines="#missing" property="role">edt</meta>
    <meta refines="creator0" property="role">edt</meta>
    <meta refines="#creator0">no property</meta>
    <meta name="primary-writing-mode" content="vertical-rl"/>
    <link rel="record" href="meta.xml" media-type="application/xml"/>
  </metadata>
  <manifest><item id="nav" href="nav.xhtml" media-type="application/xhtml+xml" properties="nav"/></manifest>
  <spine><itemref idref="nav"/></spine>
</package>
"""

LEGACY_STYLE = """<?xml version="1.0"?>
<!DOCTYPE package PUBLIC "+//ISBN 0-9673008-1-9//DTD OEB 1.2 Package//EN" "http://openebook.org/dtds/oeb-1.2/oebpkg12.dtd">
<package unique-identifier="uid">
<metadata><dc-metadata xmlns:dc="http://purl.org/dc/elements/1.1/">
<DC:Title>Old Style</DC:Title><dc:Identifier id="uid">x</dc:Identifier><DC:Language>FR</DC:Language>
</dc-metadata><x-metadata><meta name="x" content="y"/></x-metadata></metadata>
<manifest><opf:item id="a" href="a.html" media-type="text/x-oeb1-document"/></manifest></package>
"""


def curated():
    kindle = EPUB3.replace('xml:lang="ja-Latn"', "")
    cases = [
        ("2.0", EPUB2), ("2.0", EPUB2.replace("\n", "\r\n")), ("2.0", LEGACY_STYLE),
        ("3.0", EPUB3), ("3.0", kindle), ("3.0", EPUB3.replace("\n", "\r\n")), ("3.0", LEGACY_STYLE),
        ("2.0", EPUB3), ("3.0", EPUB2),
        ("3.0", EPUB3.replace("<metadata xmlns:dc", "<opf:metadata xmlns:dc").replace("</metadata>", "</opf:metadata>")),
        ("3.0", EPUB3.replace("<dc:title", "<  dc:title\t").replace("</dc:title>", "</ dc:title >")),
        ("2.0", EPUB2.replace('name="calibre:series_index"', 'name="calibre:series_index" CONTENT="4"')),
        ("3.0", EPUB3.replace("<metadata", "<METADATA ").replace("</metadata>", "</ METADATA >")),
        ("3.0", EPUB3.replace("</package>", "</package>\n<metadata><dc:title>Second</dc:title></metadata>\n")),
        # Legacy returns None: no package element, no metadata element (EPUB 2), meta without content, stray end tag.
        ("3.0", EPUB3.replace("<package", "<pkg").replace("</package>", "</pkg>")),
        ("2.0", EPUB2.replace("<metadata", "<md").replace("</metadata>", "</md>")),
        ("2.0", EPUB2.replace(' content="3"', "")),
        ("3.0", "</stray>" + EPUB3),
        ("3.0", ""),
    ]
    return cases


def edits(pieces, version, generator):
    base = pieces["data"]
    yield dict(pieces)
    extra2 = ("dc:subject" + US + "  New & <b> \"q\"  " + RS + "  id" + US + "sub" + RS +
              "calibre:rating" + US + "4" + RS + "  opf:scheme" + US + "X" + RS + "  id" + US + "isbn" + RS +
              "dc:creator" + US + "A" + RS + "  opf:role" + US + "aut" + RS + "  custom" + US + "v" + RS)
    extra3 = ("dc:creator" + US + " Name " + RS + "  role" + US + "aut" + RS + "  scheme" + US + "marc:relators" + RS +
              "  alternate-script" + US + "\u540d" + RS + "  altlang" + US + "ja" + RS + "  file-as" + US + "Doe" + RS +
              "belongs-to-collection" + US + "Series" + RS + "  collection-type" + US + "series" + RS +
              "  group-position" + US + "2" + RS + "  id" + US + "c01" + RS +
              "dc:title" + US + "T" + RS + "  id" + US + "title" + RS + "  title-type" + US + "subtitle" + RS +
              "dcterms:modified" + US + "2027" + RS + "  property" + US + "x" + RS +
              "dc:subject" + US + "S" + RS + "  scheme" + US + "only" + RS + "  altlang" + US + "en" + RS +
              "dc:identifier" + US + "978" + RS + "  identifier-type" + US + "15" + RS + "  scheme" + US + "onix" + RS)
    extra = extra3 if version.startswith("3") else extra2
    yield dict(pieces, data=base + extra)
    yield dict(pieces, data=extra + base, ids=pieces["ids"] + ["uid", "tle", "cre", "num", "sub001"])
    yield dict(pieces, data=base.replace(RS, " " + RS), tag="")
    yield dict(pieces, data=base + "dc:title" + US + "a" + US + "b" + RS)
    yield dict(pieces, data=base + "broken line" + RS)
    yield dict(pieces, data="")
    lines = [line for line in base.split(RS) if line]
    generator.shuffle(lines)
    yield dict(pieces, data=RS.join(lines) + RS)


def opf_variants(opf):
    yield opf
    yield opf.replace("</metadata>", "")
    yield opf.replace("</metadata>", "</metadata>\u3000\x1c \n\n", 1)
    yield "<metadata>" + opf


def random_opf(generator, version):
    names = ["dc:title", "dc:creator", "dc:language", "dc:identifier", "dc:subject", "meta", "link", "dc:date",
             "DC:Title", "opf:meta", "x:other", "dc:rights", "dc:contributor"]
    keys = ["id", "property", "refines", "name", "content", "scheme", "xml:lang", "opf:role", "opf:file-as",
            "xmlns:dc", "ID", "Property"]
    values = ["title", "#title", "#c0", "#c1", "c0", "c1", "role", "alternate-script", "cover", "aut", "ja",
              "a &amp; b", "&lt;x&gt;", "q&quot;", "belongs-to-collection", "dcterms:modified", "file-as", "",
              "\u65e5\u672c", "x y", "marc:relators", "zh-hans-TW", "EN", "http://h/p", "\U0001f600"]
    texts = ["", " ", "Text", "a &amp; b", "\u65e5\u672c ", "EN-gb", "x-y-z", "  lead", "t\u00df"]
    entries = []
    for _ in range(generator.randrange(0, 14)):
        name = generator.choice(names)
        attrs = []
        for key in generator.sample(keys, generator.randrange(0, 4)):
            value = generator.choice(values)
            quote = generator.choice(['"', "'", '"', "'", ""])
            if not quote:
                value = value.replace(" ", "").replace("&", "")
            equals = generator.choice(["=", "=", " = ", "= "])
            attrs.append("%s%s%s%s%s%s" % (generator.choice([" ", "  ", "\n ", "\t"]), key, equals, quote, value, quote))
        if generator.random() < 0.05:
            attrs.append(generator.choice([" novalue", ' broken="unterminated', " href=a/b"]))
        if generator.random() < 0.3:
            entries.append("<%s%s%s/>" % (name, "".join(attrs), generator.choice(["", " "])))
        else:
            entries.append("<%s%s>%s</%s>" % (name, "".join(attrs), generator.choice(texts), name))
        if generator.random() < 0.1:
            entries.append("<!-- note -->")
    uid = generator.choice(["title", "c0", "bookid", "missing"])
    metadata_attrs = generator.choice(["", ' xmlns:dc="http://purl.org/dc/elements/1.1/"', ' id="md"'])
    body = "\n    ".join(entries)
    return ('<?xml version="1.0" encoding="utf-8"?>\n<package version="%s" unique-identifier="%s">\n'
            '  <metadata%s>\n    %s\n  </metadata>\n  <manifest><item id="c1" href="a"/></manifest>\n</package>\n'
            % (version, uid, metadata_attrs, body))


def run(probe, golden, write):
    generator = random.Random(3)
    frozen = []
    cases = curated()
    for index in range(1000):
        version = "3.0" if index % 2 else "2.0"
        cases.append((version, random_opf(generator, version)))
    for index, (version, opf) in enumerate(cases):
        expected = legacy_extract(opf, version)
        actual = native_extract(probe, opf, version)
        STATS["extract"] += 1
        if expected is None:
            STATS["failed"] += 1
            assert actual is None, "native extract succeeded where legacy failed:\n" + opf
            # The editor then works with empty pieces.
            expected = {"data": "", "other": "", "ids": [], "tag": ""}
        else:
            assert actual == expected, "extract differs for\n%s\nlegacy %r\nnative %r" % (opf, expected, actual)
        record = {"version": version, "opf": opf, "pieces": expected, "applied": []}
        for edit, pieces in enumerate(edits(expected, version, generator)):
            for target in opf_variants(opf):
                legacy = legacy_apply(pieces, target, version)
                native = native_apply(probe, pieces, target, version)
                STATS["apply"] += 1
                assert native == legacy, "apply differs\n--- pieces\n%r\n--- opf\n%s\n--- legacy\n%s\n--- native\n%s" % (
                    pieces, target, legacy, native)
                if index < len(curated()) and target is opf and edit < 3:
                    record["applied"].append({"pieces": pieces, "result": legacy})
        if index < len(curated()):
            frozen.append(record)
    if write:
        GOLDEN.write_text(json.dumps(frozen, ensure_ascii=False, indent=1) + "\n", encoding="utf-8")
        return
    assert frozen == golden, "legacy metadata output no longer matches the frozen golden file"
    for record in golden:
        assert native_extract(probe, record["opf"], record["version"]) in (record["pieces"], None)
        for applied in record["applied"]:
            assert native_apply(probe, applied["pieces"], record["opf"], record["version"]) == applied["result"]


def hang_cases(probe):
    # The legacy scanner restarts at offset 0 on a '<' without a later '>' and
    # never returns; the native parser reports failure instead.
    for opf in (EPUB3 + "<", EPUB3.replace("</package>", "<!-- unterminated"), "<package><metadata><dc:title>x</dc:title"):
        assert native_extract(probe, opf, "3.0") is None
        assert native_extract(probe, opf, "2.0") is None


def main():
    write = "--write-golden" in sys.argv
    probe = Probe([arg for arg in sys.argv[1:] if not arg.startswith("--")][0])
    golden = None if write else json.loads(GOLDEN.read_text(encoding="utf-8"))
    run(probe, golden, write)
    hang_cases(probe)
    print("metadata_processor_parity: %(extract)d extracts (%(failed)d legacy failures), %(apply)d applies match" % STATS)


if __name__ == "__main__":
    main()
