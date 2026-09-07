"""Synthetic EPUB fixtures for native missing-navigation repair checks."""
import argparse
import pathlib
import re
import zipfile

from run_opf_resource_integration import epub_fixture, run


def fixtures(scratch):
    base, source, original = epub_fixture(scratch)
    ncx = b'''<?xml version="1.0" encoding="UTF-8"?>
<ncx xmlns="http://www.daisy.org/z3986/2005/ncx/" version="2005-1">
<head/><docTitle><text>Contents</text></docTitle><navMap>
<navPoint id="one" playOrder="1"><navLabel><text>Chapter &amp; text</text></navLabel><content src="../a.xhtml#start"/>
<navPoint id="two" playOrder="2"><navLabel><text>Nested entry</text></navLabel><content src="../a.xhtml#start"/></navPoint>
</navPoint></navMap></ncx>'''
    sources, all_members = {}, {}
    for variant in ('missing', 'no-ncx', 'declared', 'bad-target', 'bad-fragment', 'warning', 'epub2'):
        text = source
        members = dict(original)
        del members['OEBPS/nav.xhtml']
        if variant != 'declared':
            text = text.replace("  <item id='nav' href='nav.xhtml' media-type='application/xhtml+xml' properties='nav'/>\r\n", '')
            # A substring of 'nav' is not the navigation property token.
            text = text.replace("id='a' href='a.xhtml'", "id='a' properties='scripted-navigation' href='a.xhtml'")
        if variant not in ('no-ncx', 'warning', 'epub2'):
            text = text.replace('</manifest>', "<item id='ncx' href='Toc/toc.ncx' media-type='application/x-dtbncx+xml'/></manifest>")
            # Deliberately omit spine toc: EPUB 3 may carry an NCX for viewing
            # without rewriting its package to add the EPUB 2 spine attribute.
            members['OEBPS/Toc/toc.ncx'] = ncx
        if variant == 'bad-target':
            members['OEBPS/Toc/toc.ncx'] = ncx.replace(b'../a.xhtml#start', b'../missing.xhtml')
        if variant == 'bad-fragment':
            members['OEBPS/Toc/toc.ncx'] = ncx.replace(b'#start', b'#missing')
        members['OEBPS/a.xhtml'] = members['OEBPS/a.xhtml'].replace(b'<p>', b'<p id="start">')
        if variant == 'warning':
            members['OEBPS/a.xhtml'] = members['OEBPS/a.xhtml'].replace(b'<!DOCTYPE html>\n', b'')
        if variant == 'epub2':
            text = text.replace("version='3.0'", "version='2.0'")
        members['OEBPS/content.opf'] = b'\xff\xfe' + text.encode('utf-16-le')
        path = pathlib.Path(str(base) + '.' + variant)
        with zipfile.ZipFile(path, 'w') as archive:
            for name, data in members.items():
                archive.writestr(name, data)
        pathlib.Path(str(path) + '.opf').write_bytes(members['OEBPS/content.opf'])
        sources[variant], all_members[variant] = text, members
    return base, sources, all_members


def verify(base, sources, all_members):
    for variant in ('missing', 'no-ncx', 'declared'):
        path = str(base) + '.' + variant
        members = all_members[variant]
        with zipfile.ZipFile(path + '.viewed.epub') as archive:
            assert set(archive.namelist()) == set(members), 'Read-only export added resources'
            assert archive.read('OEBPS/content.opf') == members['OEBPS/content.opf'], 'Read-only export changed OPF'
        with zipfile.ZipFile(path + '.repaired.epub') as archive:
            assert set(archive.namelist()) - set(members) == {'OEBPS/nav.xhtml'}, 'Repair added unplanned resources'
            assert b'epub:type="toc"' in archive.read('OEBPS/nav.xhtml')
            data = archive.read('OEBPS/content.opf')
            assert data.startswith(b'\xff\xfe'), 'Repair lost original OPF encoding'
            text = re.sub(r"(<meta property='dcterms:modified'>)[^<]*(</meta>)",
                          r'\g<1>2026-09-01T00:00:00Z\2', data[2:].decode('utf-16-le'))
            if variant == 'declared':
                assert text == sources[variant], 'Existing declaration repair changed unrelated OPF'
            else:
                # Remove only the single inserted item and its own indentation.
                text, count = re.subn(r'\r\n[^\S\r\n]*<item\b[^>]*properties="nav"[^>]*/>', '', text)
                assert count == 1 and text == sources[variant], 'Repair changed more than one manifest insertion'
            for name in members:
                if name not in ('META-INF/container.xml', 'OEBPS/content.opf'):
                    assert archive.read(name) == members[name], 'Repair changed unrelated member: ' + name
    print('Missing-nav read-only export and explicit repair byte comparisons passed')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('build', type=pathlib.Path)
    run(parser.parse_args().build.resolve(), 'navigation_repair_integration_test.cpp', fixtures, verify)
