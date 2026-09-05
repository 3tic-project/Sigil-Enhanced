"""Run the real OPFResource with an existing macOS Ninja Debug build.

Reuse the application's objects and link flags with a test entry point. This
avoids mocks of the Python bridge, QTextDocument and resource persistence, and
avoids recompiling the application twice. No existing binary or settings are
overwritten. Run after `cmake --build <build> --target Sigil`.
"""
import argparse
import os
import pathlib
import shlex
import subprocess
import sys
import tempfile
import re
import zipfile


def epub_fixture(scratch):
    source = """<?xml version='1.0' encoding='UTF-16'?>
<package xmlns='http://www.idpf.org/2007/opf' xmlns:dc='http://purl.org/dc/elements/1.1/' xmlns:x='urn:publisher' version='3.0' unique-identifier='bookid'>
 <!-- keep source é 𠮷 -->
 <metadata>
  <dc:identifier id='bookid'>urn:test:opf-source</dc:identifier>
  <dc:title>Source regression</dc:title><dc:language>en</dc:language>
  <meta property='dcterms:modified'>2026-09-01T00:00:00Z</meta>
  <x:extension x:hint='a > b'><x:item href='not-a-manifest-resource.xhtml'>keep</x:item></x:extension>
 </metadata>
 <manifest>
  <x:item id='extension-only' href='not-a-manifest-resource.xhtml' media-type='application/xhtml+xml'/>
  <item id='a' href='a.xhtml' media-type='application/xhtml+xml'/>
  <item id='nav' href='nav.xhtml' media-type='application/xhtml+xml' properties='nav'/>
 </manifest>
 <spine><itemref idref='a'/></spine>
</package>
""".replace('\n', '\r\n').replace('</metadata>\r\n', '</metadata>\n')
    header = '<?xml version="1.0" encoding="utf-8"?>\n<!DOCTYPE html>\n'
    members = {
        'mimetype': b'application/epub+zip',
        'META-INF/container.xml': b'<?xml version="1.0"?><container version="1.0" xmlns="urn:oasis:names:tc:opendocument:xmlns:container"><rootfiles><rootfile full-path="OEBPS/content.opf" media-type="application/oebps-package+xml"/></rootfiles></container>',
        'OEBPS/content.opf': b'\xff\xfe' + source.encode('utf-16-le'),
        'OEBPS/a.xhtml': (header + '<html xmlns="http://www.w3.org/1999/xhtml"><head><title>Chapter</title></head><body><p>Original paragraph</p></body></html>').encode(),
        'OEBPS/nav.xhtml': (header + '<html xmlns="http://www.w3.org/1999/xhtml" xmlns:epub="http://www.idpf.org/2007/ops"><head><title>Contents</title></head><body><nav epub:type="toc"><h1>Contents</h1><ol><li><a href="a.xhtml">Chapter</a></li></ol></nav></body></html>').encode(),
    }
    path = scratch / 'fixture.epub'
    with zipfile.ZipFile(path, 'w') as archive:
        for name, data in members.items():
            archive.writestr(name, data)
    pathlib.Path(str(path) + '.opf').write_bytes(members['OEBPS/content.opf'])
    return path, source, members


def verify_epub_exports(path, source, members):
    with zipfile.ZipFile(str(path) + '.unchanged.epub') as archive:
        assert archive.read('OEBPS/content.opf') == members['OEBPS/content.opf'], 'Unchanged normal export modified OPF bytes'
    with zipfile.ZipFile(str(path) + '.edited.epub') as archive:
        data = archive.read('OEBPS/content.opf')
        assert data.startswith(b'\xff\xfe'), 'Export lost the original UTF-16 BOM'
        exported = data[2:].decode('utf-16-le')
        expected = re.sub(r"(<meta property='dcterms:modified'>)[^<]*(</meta>)",
                          r'\g<1>2026-09-01T00:00:00Z\2', exported)
        assert expected == source, 'Body edit changed unrelated OPF bytes'
        assert b'Changed paragraph' in archive.read('OEBPS/a.xhtml'), 'Body edit was not exported'
        for name in ('mimetype', 'OEBPS/nav.xhtml'):
            assert archive.read(name) == members[name], 'Export changed unrelated resource: ' + name
    print('Native EPUB import and normal-export byte comparisons passed')


def run(build):
    if sys.platform != 'darwin':
        raise RuntimeError('This integration runner currently supports macOS only')
    root = pathlib.Path(__file__).resolve().parents[1]
    cache = {}
    for line in (build / 'CMakeCache.txt').read_text().splitlines():
        if line and not line.startswith(('#', '//')) and '=' in line:
            key, value = line.split('=', 1)
            cache[key.split(':', 1)[0]] = value
    if cache.get('CMAKE_GENERATOR') != 'Ninja' or cache.get('CMAKE_BUILD_TYPE') != 'Debug':
        raise RuntimeError('This integration runner requires a Ninja Debug build')
    if pathlib.Path(cache['CMAKE_HOME_DIRECTORY']).resolve() != root:
        raise RuntimeError('Build directory belongs to a different source worktree')
    ninja = cache['CMAKE_MAKE_PROGRAM']
    commands = subprocess.check_output([ninja, '-t', 'commands', 'Sigil'], cwd=build, text=True).splitlines()
    compile_line = next(line for line in commands if ' -c ' in line
                        and shlex.split(line)[-1].endswith('/src/main.cpp'))
    link_line = next(line for line in reversed(commands) if ' -o bin/Sigil.app/Contents/MacOS/Sigil ' in line)
    with tempfile.TemporaryDirectory(prefix='opf-native-') as directory:
        scratch = pathlib.Path(directory)
        (scratch / 'workspace').mkdir()
        fixture, source, members = epub_fixture(scratch)
        test_object = scratch / 'test.o'
        compile_args = shlex.split(compile_line)
        for flag, path in [('-o', test_object), ('-MF', scratch / 'test.d'),
                           ('-MT', test_object), ('-c', root / 'tests/opf_resource_integration_test.cpp')]:
            compile_args[compile_args.index(flag) + 1] = str(path)
        subprocess.run(compile_args, cwd=build, check=True)
        link_args = shlex.split(link_line)
        # Keep only the compiler invocation, excluding application packaging.
        start = link_args.index('&&') + 1 if link_args[0] == ':' else 0
        end = link_args.index('&&', start) if '&&' in link_args[start:] else len(link_args)
        link_args = link_args[start:end]
        main_object = next(i for i, value in enumerate(link_args) if value.endswith('/main.cpp.o'))
        link_args[main_object] = str(test_object)
        # Keep the harness isolated inside the build; do not replace Sigil.
        executable_dir = build / 'bin/Sigil.app/Contents/MacOS'
        with tempfile.TemporaryDirectory(prefix='opf-test-', dir=executable_dir) as bin_directory:
            executable = pathlib.Path(bin_directory) / 'opf_resource_test'
            link_args[link_args.index('-o') + 1] = str(executable)
            subprocess.run(link_args, cwd=build, check=True)
            environment = dict(os.environ, QT_QPA_PLATFORM='offscreen', SIGIL_PREFS_DIR=str(scratch))
            # Use source modules plus built runtime dependencies for the nested
            # executable; the embedded interpreter uses the same linked Python.
            environment['SIGIL_TEST_PYTHON_ROOT'] = str(build / 'bin/Sigil.app/Contents/python3lib')
            environment.pop('PYTHONPATH', None)
            subprocess.run([executable, root, fixture], env=environment, check=True, timeout=30)
            verify_epub_exports(fixture, source, members)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('build', type=pathlib.Path)
    run(parser.parse_args().build.resolve())
