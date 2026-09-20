"""Run the real gumbo syntax checks against an EPUB with an empty resource.

Reuse the application's objects and link flags with a test entry point so the
Book-level guard and the gumbo error checks run against real code. No existing
binary or settings are overwritten. Run after `cmake --build <build> --target
Sigil`.
"""
import argparse
import pathlib
import zipfile

from run_opf_resource_integration import run


def epub_fixture(scratch):
    opf = """<?xml version='1.0' encoding='UTF-8'?>
<package xmlns='http://www.idpf.org/2007/opf' xmlns:dc='http://purl.org/dc/elements/1.1/' version='3.0' unique-identifier='bookid'>
 <metadata>
  <dc:identifier id='bookid'>urn:test:gumbo-empty-source</dc:identifier>
  <dc:title>Empty source regression</dc:title><dc:language>en</dc:language>
  <meta property='dcterms:modified'>2026-09-20T00:00:00Z</meta>
 </metadata>
 <manifest>
  <item id='a' href='a.xhtml' media-type='application/xhtml+xml'/>
  <item id='empty' href='empty.xhtml' media-type='application/xhtml+xml'/>
 </manifest>
 <spine><itemref idref='a'/></spine>
</package>
"""
    header = '<?xml version="1.0" encoding="utf-8"?>\n<!DOCTYPE html>\n'
    members = {
        'mimetype': b'application/epub+zip',
        'META-INF/container.xml': b'<?xml version="1.0"?><container version="1.0" xmlns="urn:oasis:names:tc:opendocument:xmlns:container"><rootfiles><rootfile full-path="OEBPS/content.opf" media-type="application/oebps-package+xml"/></rootfiles></container>',
        'OEBPS/content.opf': opf.encode('utf-8'),
        'OEBPS/a.xhtml': (header + '<html xmlns="http://www.w3.org/1999/xhtml"><head><title>Chapter</title></head><body><p>Paragraph</p></body></html>').encode(),
        # The regression fixture: a manifest resource with no content at all.
        'OEBPS/empty.xhtml': b'',
    }
    path = scratch / 'fixture.epub'
    with zipfile.ZipFile(path, 'w') as archive:
        for name, data in members.items():
            archive.writestr(name, data)
    return path, '', members


def verify(fixture, source, members):
    print('Empty-source gumbo regression fixture verified')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('build', type=pathlib.Path)
    run(
        parser.parse_args().build.resolve(),
        test_entry='gumbo_empty_source_integration_test.cpp',
        build_fixture=epub_fixture,
        verify=verify,
        run_timeout=60,
    )
