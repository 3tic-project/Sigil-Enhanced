"""Exercise no-op save actions and Metadata Editor cancellation in MainWindow."""

import argparse
import hashlib
import os
import pathlib
import shutil
import sys
import zipfile

from run_opf_resource_integration import epub_fixture, run
from run_cmoa_epub_integration import EXPECTED_SAMPLE_SHA256


def epub2_fixture(scratch):
    opf = b"""<?xml version='1.0' encoding='UTF-8'?>
<package xmlns='http://www.idpf.org/2007/opf' xmlns:dc='http://purl.org/dc/elements/1.1/' version='2.0' unique-identifier='bookid'>
 <!-- EPUB 2 no-op source marker -->
 <metadata>
  <dc:identifier id='bookid'>urn:test:main-window-epub2</dc:identifier>
  <dc:title>MainWindow EPUB 2</dc:title><dc:language>en</dc:language>
 </metadata>
 <manifest>
  <item id='a' href='a.xhtml' media-type='application/xhtml+xml'/>
  <item id='ncx' href='toc.ncx' media-type='application/x-dtbncx+xml'/>
 </manifest>
 <spine toc='ncx'><itemref idref='a'/></spine>
</package>
"""
    members = {
        "mimetype": b"application/epub+zip",
        "META-INF/container.xml": b"""<?xml version='1.0'?>
<container version='1.0' xmlns='urn:oasis:names:tc:opendocument:xmlns:container'>
 <rootfiles><rootfile full-path='OEBPS/content.opf' media-type='application/oebps-package+xml'/></rootfiles>
</container>""",
        "OEBPS/content.opf": opf,
        "OEBPS/a.xhtml": b"""<?xml version='1.0' encoding='utf-8'?>
<!DOCTYPE html><html xmlns='http://www.w3.org/1999/xhtml'><head><title>A</title></head><body><p>EPUB 2</p></body></html>""",
        "OEBPS/toc.ncx": b"""<?xml version='1.0' encoding='UTF-8'?>
<ncx xmlns='http://www.daisy.org/z3986/2005/ncx/' version='2005-1'>
 <head><meta name='dtb:uid' content='urn:test:main-window-epub2'/></head>
 <docTitle><text>MainWindow EPUB 2</text></docTitle>
 <navMap><navPoint id='n1' playOrder='1'><navLabel><text>A</text></navLabel><content src='a.xhtml'/></navPoint></navMap>
</ncx>""",
    }
    path = scratch / "fixture-epub2.epub"
    with zipfile.ZipFile(path, "w") as archive:
        for name, data in members.items():
            archive.writestr(name, data)
    return path, None, members


def cmoa_source():
    value = os.environ.get("SIGIL_CMOA_TEST_EPUB", "")
    source = pathlib.Path(value).expanduser()
    if not value or not source.is_file():
        print("SKIP: set SIGIL_CMOA_TEST_EPUB to the private Cmoa EPUB", file=sys.stderr)
        raise SystemExit(77)
    digest = hashlib.sha256(source.read_bytes()).hexdigest()
    if digest != EXPECTED_SAMPLE_SHA256:
        raise SystemExit(
            "private Cmoa EPUB fingerprint mismatch: expected "
            f"{EXPECTED_SAMPLE_SHA256}, got {digest}"
        )
    return source, digest


def cmoa_fixture(source, digest):
    def build(scratch):
        target = scratch / "cmoa-test.epub"
        shutil.copy2(source, target)
        return target, digest, None

    return build


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("build", type=pathlib.Path)
    parser.add_argument(
        "--fixture", choices=("epub2", "epub3", "cmoa"), default="epub3"
    )
    args = parser.parse_args()
    fixture = epub_fixture
    verify = lambda *unused: None
    if args.fixture == "epub2":
        fixture = epub2_fixture
    elif args.fixture == "cmoa":
        source, digest = cmoa_source()
        fixture = cmoa_fixture(source, digest)

        def verify(_path, expected, _members):
            if hashlib.sha256(source.read_bytes()).hexdigest() != expected:
                raise AssertionError("MainWindow no-op test changed the private Cmoa EPUB")

    run(
        args.build.resolve(),
        "main_window_noop_integration_test.cpp",
        fixture,
        verify,
        direct_app_executable=True,
    )
