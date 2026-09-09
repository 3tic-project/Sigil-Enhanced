"""Exercise EditTOC hierarchy operations through the real EPUB resources."""
import argparse
import pathlib
import zipfile

from run_opf_resource_integration import epub_fixture, run


def fixture(scratch):
    path, source, members = epub_fixture(scratch)
    source = source.replace(
        "  <item id='nav' href='nav.xhtml' media-type='application/xhtml+xml' properties='nav'/>\r\n",
        "  <item id='nav' href='nav.xhtml' media-type='application/xhtml+xml' properties='nav'/>\r\n"
        "  <item id='ncx' href='toc.ncx' media-type='application/x-dtbncx+xml'/>\r\n")
    source = source.replace('<spine>', "<spine toc='ncx'>")
    nav = '''<?xml version="1.0" encoding="utf-8"?>
<!DOCTYPE html>
<html xmlns="http://www.w3.org/1999/xhtml" xmlns:epub="http://www.idpf.org/2007/ops">
<head><title>Contents</title></head><body>
<!-- keep-before-toc -->
<nav epub:type="toc" id="custom-toc" class="keep"><h1><span>Contents</span></h1><ol data-owner="fixture">
<li id="a"><a href="a.xhtml#a">A</a><ol>
<li id="b"><a href="a.xhtml#b">B</a></li>
<li id="c"><a href="a.xhtml#c"><span>C</span></a><ol><li id="c1"><a href="a.xhtml#c1">C1</a></li></ol></li>
<li id="d"><a href="a.xhtml#d">D</a></li>
<li id="e"><a href="a.xhtml#e">E</a></li>
<li id="f"><a href="a.xhtml#f">F</a></li>
</ol></li>
<li id="x"><a href="a.xhtml#x">X</a></li>
</ol></nav>
<!-- keep-between-navs -->
<nav epub:type="landmarks" id="landmarks"><ol><li><a epub:type="bodymatter" href="a.xhtml#a">Body</a></li></ol></nav>
<nav epub:type="page-list" id="pages"><ol><li><a href="a.xhtml#p1">1</a></li></ol></nav>
</body></html>'''.encode()
    ncx = '''<?xml version="1.0" encoding="UTF-8"?>
<ncx xmlns="http://www.daisy.org/z3986/2005/ncx/" version="2005-1">
<head><meta name="dtb:uid" content="urn:test:opf-source"/><meta name="fixture:keep" content="yes"/></head>
<docTitle><text>Fixture contents</text></docTitle><docAuthor><text>Fixture author</text></docAuthor>
<navMap>
<navPoint id="a" playOrder="1"><navLabel><text>A</text></navLabel><content src="a.xhtml#a"/>
<navPoint id="b" playOrder="2"><navLabel><text>B</text></navLabel><content src="a.xhtml#b"/></navPoint>
<navPoint id="c" playOrder="3"><navLabel><text>C</text></navLabel><content src="a.xhtml#c"/><navPoint id="c1" playOrder="4"><navLabel><text>C1</text></navLabel><content src="a.xhtml#c1"/></navPoint></navPoint>
<navPoint id="d" playOrder="5"><navLabel><text>D</text></navLabel><content src="a.xhtml#d"/></navPoint>
<navPoint id="e" playOrder="6"><navLabel><text>E</text></navLabel><content src="a.xhtml#e"/></navPoint>
<navPoint id="f" playOrder="7"><navLabel><text>F</text></navLabel><content src="a.xhtml#f"/></navPoint>
</navPoint>
<navPoint id="x" playOrder="8"><navLabel><text>X</text></navLabel><content src="a.xhtml#x"/></navPoint>
</navMap><pageList><navLabel><text>Pages</text></navLabel></pageList></ncx>'''.encode()
    members['OEBPS/content.opf'] = b'\xff\xfe' + source.encode('utf-16-le')
    members['OEBPS/nav.xhtml'] = nav
    members['OEBPS/toc.ncx'] = ncx
    members['OEBPS/a.xhtml'] = members['OEBPS/a.xhtml'].replace(
        b'<p>', b'<p id="a"><span id="b"/><span id="c"/><span id="c1"/>'
                b'<span id="d"/><span id="e"/><span id="f"/><span id="x"/>'
                b'<span id="p1"/>')
    with zipfile.ZipFile(path, 'w') as archive:
        for name, data in members.items():
            archive.writestr(name, data)
    epub2_source = source.replace("version='3.0'", "version='2.0'")
    epub2_source = epub2_source.replace(
        "  <item id='nav' href='nav.xhtml' media-type='application/xhtml+xml' properties='nav'/>\r\n", '')
    epub2_members = dict(members)
    del epub2_members['OEBPS/nav.xhtml']
    epub2_members['OEBPS/content.opf'] = (
        b'\xff\xfe' + epub2_source.encode('utf-16-le'))
    with zipfile.ZipFile(str(path) + '.epub2', 'w') as archive:
        for name, data in epub2_members.items():
            archive.writestr(name, data)
    large_members = dict(members)
    siblings = ['<li id="group"><a href="a.xhtml#a">Group</a><ol>'
                '<li id="deep"><a href="a.xhtml#a">Deep</a></li></ol></li>']
    siblings.extend(
        f'<li id="n{index}"><a href="a.xhtml#a">N{index}</a></li>'
        for index in range(4, 10000))
    large_members['OEBPS/nav.xhtml'] = (
        '<?xml version="1.0" encoding="utf-8"?>\n<!DOCTYPE html>\n'
        '<html xmlns="http://www.w3.org/1999/xhtml" '
        'xmlns:epub="http://www.idpf.org/2007/ops"><head><title>Large</title></head><body>'
        '<nav epub:type="toc"><h1>Large</h1><ol><li id="a"><a href="a.xhtml#a">A</a><ol>'
        + ''.join(siblings)
        + '</ol></li><li id="x"><a href="a.xhtml#a">X</a></li></ol></nav></body></html>'
    ).encode()
    with zipfile.ZipFile(str(path) + '.large', 'w') as archive:
        for name, data in large_members.items():
            archive.writestr(name, data)
    return path, source, members


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('build', type=pathlib.Path)
    run(parser.parse_args().build.resolve(), 'edit_toc_hierarchy_integration_test.cpp',
        fixture, lambda *args: None)
