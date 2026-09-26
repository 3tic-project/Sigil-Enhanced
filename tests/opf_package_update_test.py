"""Behavioral source-preservation checks for the structured plugin API."""
import json
import pathlib
import sys
import unittest

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1] / 'tests/fixtures'))
from lxml import etree
from opf_package_update_legacy import apply_update


SOURCE = """<?xml version='1.0' encoding='UTF-8'?>
<?publisher keep?>
<package xmlns='http://www.idpf.org/2007/opf' xmlns:dc='http://purl.org/dc/elements/1.1/' xmlns:x='urn:private' version='3.0'>
  <metadata>
    <!-- title group -->
    <dc:identifier id='bookid'>urn:test</dc:identifier>
    <dc:title id='title'>Old</dc:title>
    <x:opaque x:key='keep'><x:title>Do not flatten</x:title></x:opaque>
  </metadata>
  <x:manifest><x:item id='decoy' href='a.xhtml'/></x:manifest>
  <manifest>
    <!-- resources -->
    <item id='a' href='a.xhtml' media-type='application/xhtml+xml' x:hint='keep'/>
    <item id='b' href='b.xhtml' media-type='application/xhtml+xml'/>
  </manifest>
  <spine page-progression-direction='ltr'>
    <!-- reading order -->
    <itemref idref='a' id='ref-a' x:hint='keep'/>
    <x:itemref idref='decoy'/>
    <itemref idref='b'/>
  </spine>
</package>
""".replace('\n', '\r\n', 4)


def update(operation, source=SOURCE, **payload):
    return apply_update(source, operation, json.dumps(payload, ensure_ascii=False))


def metadata(title='Old'):
    return [dict(name='dc:identifier', content='urn:test', attributes={'id': 'bookid'}),
            dict(name='dc:title', content=title, attributes={'id': 'title'})]


class PackageUpdates(unittest.TestCase):
    def test_metadata_noop_exact_source(self):
        self.assertEqual(update('metadata', items=metadata()), SOURCE)

    def test_metadata_one_value(self):
        value = '新しい e\u0301 & 𠮷'
        result = update('metadata', items=metadata(value))
        self.assertEqual(result, SOURCE.replace('>Old<', '>新しい e\u0301 &amp; 𠮷<'))

    def test_metadata_refinement_and_deletion(self):
        items = metadata() + [dict(name='meta', content='main', attributes={'property': 'title-type', 'refines': '#title'})]
        result = update('metadata', items=items)
        self.assertIn('refines="#title"', result)
        self.assertIn("<x:opaque x:key='keep'><x:title>Do not flatten</x:title></x:opaque>", result)
        self.assertEqual(update('metadata', result, items=items), result)
        removed = update('metadata', items=metadata()[:1])
        self.assertEqual(removed, SOURCE.replace("<dc:title id='title'>Old</dc:title>", ''))

    def test_metadata_cdata_and_comment(self):
        source = SOURCE.replace('>Old<', '><![CDATA[Old]]><')
        self.assertEqual(update('metadata', source, items=metadata('New')), source.replace('CDATA[Old]', 'CDATA[New]'))

    def test_local_namespace_declarations(self):
        items = metadata() + [dict(name='custom:date', content='2026', attributes={'xmlns:custom': 'urn:date', 'custom:event': 'modified'})]
        result = update('metadata', items=items)
        root = etree.fromstring(result.encode())
        date = root.find('.//{urn:date}date')
        self.assertEqual(date.get('{urn:date}event'), 'modified')
        self.assertNotIn('xmlns:xmlns', result)
        with self.assertRaises(ValueError):
            update('metadata', items=items + [dict(name='custom:date', content='not in scope')])

    def test_noncanonical_prefixes(self):
        source = SOURCE.replace('xmlns:dc=', 'xmlns:books=').replace('<dc:', '<books:').replace('</dc:', '</books:')
        result = update('metadata', source, items=metadata('New'))
        self.assertEqual(result, source.replace('>Old<', '>New<'))

    def test_spine_noop_and_optional_empty_api_values(self):
        items = [dict(idref='a', id='ref-a', linear='', properties=''), dict(idref='b', linear='', properties='')]
        self.assertEqual(update('spine', items=items, attributes={}), SOURCE)

    def test_spine_reorder_retains_extensions(self):
        first = "<itemref idref='a' id='ref-a' x:hint='keep'/>"
        second = "<itemref idref='b'/>"
        expected = SOURCE.replace(first, '<swap/>').replace(second, first).replace('<swap/>', second)
        self.assertEqual(update('spine', items=[dict(idref='b'), dict(idref='a', id='ref-a')]), expected)

    def test_spine_attributes_are_local(self):
        result = update('spine', items=[dict(idref='a', id='ref-a'), dict(idref='b')],
                        attributes={'page-progression-direction': 'rtl'})
        self.assertEqual(result, SOURCE.replace("page-progression-direction='ltr'", "page-progression-direction='rtl'"))

    def test_manifest_noop_and_relocation(self):
        self.assertEqual(update('manifest'), SOURCE)
        result = update('manifest', relocations=[dict(original_href='a.xhtml', target_href='Text/a%20b.xhtml')])
        self.assertEqual(result, SOURCE.replace("id='a' href='a.xhtml'", "id='a' href='Text/a%20b.xhtml'"))

    def test_manifest_add_remove_idempotency(self):
        change = dict(removals=['b.xhtml'], additions=[{'id': 'c', 'href': 'c.xhtml', 'media-type': 'application/xhtml+xml', 'properties': 'scripted'}])
        result = update('manifest', **change)
        self.assertEqual(update('manifest', result, **change), result)
        self.assertIn("<!-- resources -->", result)
        self.assertNotIn("id='b'", result)
        self.assertIn('properties="scripted"', result)
        self.assertIn("x:hint='keep'", result)

    def test_manifest_collisions(self):
        for change in [dict(relocations=[dict(original_href='a.xhtml', target_href='b.xhtml')]),
                       dict(additions=[{'id': 'a', 'href': 'c.xhtml', 'media-type': 'application/xhtml+xml'}]),
                       dict(additions=[{'id': 'c', 'href': 'a.xhtml', 'media-type': 'application/xhtml+xml'}])]:
            with self.subTest(change=change), self.assertRaises(ValueError):
                update('manifest', **change)

    def test_invalid_payloads(self):
        cases = [('metadata', dict(items=[dict(name='dc:title')])),
                 ('metadata', dict(items=[dict(name='dc:title', content='x', attributes=[])])),
                 ('metadata', dict(items=[dict(name='dc:title', content='x', attributes={'id': 4})])),
                 ('metadata', dict(items=[dict(name='unknown:title', content='x')])),
                 ('metadata', dict(items=[dict(name='bad:name:again', content='x')])),
                 ('spine', dict(items=[dict(idref=7)])),
                 ('spine', dict(items=[dict(idref='a', linear=7)])),
                 ('spine', dict(items=[], attributes={'toc': 7})),
                 ('manifest', dict(additions=[dict(id='c', href='c.xhtml')])),
                 ('manifest', dict(removals=[7])), ('unknown', {})]
        for operation, payload in cases:
            with self.subTest(operation=operation, payload=payload), self.assertRaises(ValueError):
                update(operation, **payload)

    def test_ambiguous_or_malformed_structure(self):
        for source in ['<package>', SOURCE.replace('</manifest>', '</manifest><manifest/>'),
                       SOURCE.replace("id='b' href='b.xhtml'", "id='a' href='b.xhtml'")]:
            with self.subTest(source=source), self.assertRaises(ValueError):
                update('manifest', source)

    def test_custom_entity_edit_rejected(self):
        source = SOURCE.replace("<?publisher keep?>", '<!DOCTYPE package [<!ENTITY x "Old">]>').replace('>Old<', '>&x;<')
        with self.assertRaises(ValueError):
            update('metadata', source, items=metadata('New'))


if __name__ == '__main__':
    unittest.main()
