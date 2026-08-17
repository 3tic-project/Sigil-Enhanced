#!/usr/bin/env python3

import sys
from pathlib import Path


KINDLE_STYLE_OPF = """<?xml version="1.0" encoding="utf-8"?>
<package xmlns="http://www.idpf.org/2007/opf" xmlns:dc="http://purl.org/dc/elements/1.1/" version="3.0" unique-identifier="bookid" prefix="marc: http://id.loc.gov/vocabulary/">
  <metadata>
    <dc:identifier id="bookid">urn:asin:B00A2HJ45I</dc:identifier>
    <dc:title id="title">たとえばそれが恋なら (角川ルビー文庫)</dc:title>
    <meta refines="#title" property="alternate-script">タトエバソレガコイナラ</meta>
    <dc:creator id="creator0">白城 るた</dc:creator>
    <meta refines="#creator0" property="role" scheme="marc:relators">aut</meta>
    <meta refines="#creator0" property="alternate-script">シラキ ルタ</meta>
    <dc:language>ja</dc:language>
    <dc:publisher>角川書店</dc:publisher>
    <dc:date>2008-04-01</dc:date>
    <meta property="dcterms:modified">2026-08-17T14:09:55Z</meta>
    <meta name="primary-writing-mode" content="vertical-rl"/>
  </metadata>
  <manifest>
    <item href="nav.xhtml" id="nav" media-type="application/xhtml+xml" properties="nav"/>
  </manifest>
  <spine>
    <itemref idref="nav"/>
  </spine>
</package>
"""


def require(condition, message):
    if not condition:
        raise AssertionError(message)


def main():
    repo = Path(sys.argv[1]).resolve() if len(sys.argv) > 1 else Path(__file__).resolve().parents[1]
    sys.path.insert(0, str(repo / "src/Resource_Files/plugin_launchers/python"))
    sys.path.insert(0, str(repo / "src/Resource_Files/python3lib"))

    from metaproc3 import process_metadata

    mdp = process_metadata(KINDLE_STYLE_OPF)
    require(mdp is not None, "alternate-script without xml:lang must not abort metadata parsing")

    data = mdp.get_recognized_metadata()
    other = mdp.get_other_meta_xml()
    require("dc:title" in data, "recognized metadata must include the title")
    require("タトエバソレガコイナラ" in data, "title alternate-script must stay attached to the title")
    require("シラキ ルタ" in data, "creator alternate-script must stay attached to the creator")
    require("urn:asin:B00A2HJ45I" in other, "the unique-identifier stays out of the editable list")
    require(mdp.get_metadata_tag().startswith("<metadata"), "metadata opening tag must still be produced")

    # BCP-47 tags with more than one hyphen used to crash language cleanup.
    opf = KINDLE_STYLE_OPF.replace("<dc:language>ja</dc:language>",
                                   "<dc:language>zh-Hans-CN</dc:language>")
    require(process_metadata(opf) is not None, "multi-subtag language codes must parse")
    print("metaproc3_alternate_script: ok")


if __name__ == "__main__":
    main()
