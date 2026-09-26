"""Behavioral tests for the source-preserving package-model update service."""
import pathlib
import sys
import unittest

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1] /
                       "src/Resource_Files/python3lib"))
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1] /
                       "src/Resource_Files/plugin_launchers/python"))
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1] / "tests/fixtures"))
from opf_source_legacy import Document, apply_model_update, model_xml, add_navigation_manifest


SOURCE = '''<?xml version='1.0' encoding='UTF-8'?>
<?publisher keep="this"?>
<package xmlns="http://www.idpf.org/2007/opf" xmlns:dc="http://purl.org/dc/elements/1.1/"
 xmlns:x="urn:publisher" version='3.0' unique-identifier='bookid'>
 <!-- metadata group -->
 <metadata>
  <dc:identifier id='bookid'>urn:test:本</dc:identifier>
  <dc:title id='title'>A &amp; B</dc:title>
  <meta property='dcterms:modified'>2026-09-01T00:00:00Z</meta>
  <!-- publisher extension -->
  <x:extra x:hint='a > b'><x:child>保留𠮷</x:child></x:extra>
 </metadata>
 <!-- manifest group -->
 <manifest x:hint='untouched'>
  <item media-type='application/xhtml+xml' href='a.xhtml' id='a'/>
  <!-- chapter b -->
  <item id='b' href='b.xhtml' media-type='application/xhtml+xml'/>
 </manifest>
 <spine toc='ncx'><itemref idref='a'/><!-- between --><itemref idref='b'/></spine>
 <x:unmodeled>keep me</x:unmodeled>
</package>
'''.replace('\n', '\r\n')


def model(source=SOURCE):
    # A model may omit extensions and comments; these omissions are not edits.
    import re
    value = re.sub(r'<!--.*?-->|<\?.*?\?>', '', source, flags=re.S)
    value = re.sub(r'<x:extra.*?</x:extra>|<x:unmodeled>.*?</x:unmodeled>', '', value, flags=re.S)
    return value.replace(" x:hint='untouched'", "").replace('\r\n', '\n')


class SourceUpdateTest(unittest.TestCase):
    def setUp(self):
        self.before = model()

    def apply(self, after, source=SOURCE, before=None):
        return apply_model_update(source, before or self.before, after)

    def test_noop_is_exact_source(self):
        self.assertEqual(self.apply(self.before), SOURCE)
        reordered = self.before.replace("idref='a'", 'idref="a"')
        self.assertEqual(self.apply(reordered), SOURCE)

    def test_title_changes_only_value(self):
        after = self.before.replace('A &amp; B', '日本𠮷 &lt; C')
        self.assertEqual(self.apply(after), SOURCE.replace('A &amp; B', '日本𠮷 &lt; C'))

    def test_modified_changes_only_unrefined_value(self):
        extra = "<meta property='dcterms:modified' refines='#title'>old</meta>"
        source = SOURCE.replace('</metadata>', extra + '</metadata>')
        before = model(source)
        after = before.replace('2026-09-01T00:00:00Z', '2026-09-05T10:00:00Z')
        self.assertEqual(self.apply(after, source, before),
                         source.replace('2026-09-01T00:00:00Z', '2026-09-05T10:00:00Z'))

    def test_rename_keeps_quotes_attribute_order_and_comments(self):
        self.assertEqual(self.apply(self.before.replace('a.xhtml', 'new.xhtml')),
                         SOURCE.replace('a.xhtml', 'new.xhtml'))

    def test_attribute_escape(self):
        after = self.before.replace('a.xhtml', 'a&apos;b&amp;c.xhtml')
        self.assertEqual(self.apply(after), SOURCE.replace('a.xhtml', 'a&apos;b&amp;c.xhtml'))

    def test_spine_reorder_keeps_other_source(self):
        after = self.before.replace("<itemref idref='a'/><itemref idref='b'/>",
                                    "<itemref idref='b'/><itemref idref='a'/>")
        expected = SOURCE.replace("<itemref idref='a'/><!-- between --><itemref idref='b'/>",
                                  "<itemref idref='b'/><!-- between --><itemref idref='a'/>")
        self.assertEqual(self.apply(after), expected)

    def test_delete_does_not_delete_adjacent_comments_or_extensions(self):
        removed = "<item id='b' href='b.xhtml' media-type='application/xhtml+xml'/>"
        self.assertEqual(self.apply(self.before.replace(removed, '')), SOURCE.replace(removed, ''))

    def test_insert_preserves_existing_nodes_and_newline_style(self):
        item = "<item id='c' href='c.xhtml' media-type='application/xhtml+xml'/>"
        result = self.apply(self.before.replace('</manifest>', item + '</manifest>'))
        self.assertIn("\r\n  " + item, result)
        self.assertEqual(result.replace('\r\n  ' + item, ''), SOURCE)

    def test_new_xhtml_does_not_inherit_metadata_only_namespaces(self):
        source = ('<package xmlns="http://www.idpf.org/2007/opf">'
                  '<metadata xmlns:dc="http://purl.org/dc/elements/1.1/" '
                  'xmlns:opf="http://www.idpf.org/2007/opf">'
                  '<dc:title>Test</dc:title></metadata><manifest/><spine/></package>')
        before = model_xml(source)
        after = before.replace('</manifest>',
                               '<item id="new" href="new.xhtml" media-type="application/xhtml+xml"/>'
                               '</manifest>').replace('</spine>', '<itemref idref="new"/></spine>')
        result = self.apply(after, source, before)
        self.assertIn('<item id="new" href="new.xhtml" media-type="application/xhtml+xml"/>', result)
        self.assertIn('<itemref idref="new"/>', result)
        self.assertEqual(result.count('xmlns:dc='), 1)
        self.assertEqual(result.count('xmlns:opf='), 1)
        self.assertEqual(Document(result).root.children[1].children[0].name,
                         'http://www.idpf.org/2007/opf|item')

    def test_model_keeps_section_and_leaf_namespace_declarations(self):
        from lxml import etree
        source = ('<package xmlns="http://www.idpf.org/2007/opf" version="2.0">'
                  '<metadata xmlns:dc="http://purl.org/dc/elements/1.1/" '
                  'xmlns:calibre="http://calibre.kovidgoyal.net/2009/metadata">'
                  '<dc:title>T</dc:title><calibre:series>S</calibre:series>'
                  '<leaf:note xmlns:leaf="urn:leaf" leaf:kind="k">n</leaf:note></metadata>'
                  '<manifest xmlns:m="urn:m"><item id="a" href="a.xhtml" media-type="text/css" m:x="1"/>'
                  '</manifest><spine/></package>')
        before = model_xml(source)
        root = etree.fromstring(before.encode())
        metadata = root.find('{http://www.idpf.org/2007/opf}metadata')
        self.assertEqual(metadata.find('{http://calibre.kovidgoyal.net/2009/metadata}series').text, 'S')
        self.assertEqual(metadata.find('{urn:leaf}note').get('{urn:leaf}kind'), 'k')
        self.assertEqual(root.find('.//{http://www.idpf.org/2007/opf}item').get('{urn:m}x'), '1')
        self.assertEqual(self.apply(before, source, before), source)

    def test_new_item_keeps_namespaces_used_by_names_or_curie_values(self):
        source = ('<package xmlns="http://www.idpf.org/2007/opf">'
                  '<metadata/><manifest/><spine/></package>')
        before = model_xml(source)
        after = before.replace('</manifest>',
                               '<item id="new" href="new.xhtml" media-type="application/xhtml+xml" '
                               'xmlns:custom="urn:custom" custom:flag="yes" '
                               'properties="opf:sample"/></manifest>')
        result = self.apply(after, source, before)
        item = Document(result).root.children[1].children[0]
        self.assertEqual(item.attrs['urn:custom|flag'], 'yes')
        self.assertIn('xmlns:custom="urn:custom"', result)
        self.assertIn('xmlns:opf="http://www.idpf.org/2007/opf"', result)
        self.assertNotIn('xmlns:dc=', result)
        qualified = before.replace('</manifest>',
                                   '<item id="new" href="new.xhtml" media-type="application/xhtml+xml" '
                                   'opf:flag="yes"/></manifest>')
        result = self.apply(qualified, source, before)
        item = Document(result).root.children[1].children[0]
        self.assertEqual(item.attrs['http://www.idpf.org/2007/opf|flag'], 'yes')
        self.assertIn('xmlns:opf="http://www.idpf.org/2007/opf"', result)
        self.assertNotIn('xmlns:dc=', result)

    def test_new_attribute_and_namespace(self):
        after = self.before.replace("idref='a'", "idref='a' linear='no'")
        self.assertEqual(self.apply(after), SOURCE.replace("idref='a'", "idref='a' linear=\"no\""))
        after = self.before.replace("<dc:title id='title'", "<dc:title id='title' xml:lang='ja'")
        self.assertEqual(self.apply(after), SOURCE.replace("<dc:title id='title'", "<dc:title id='title' xml:lang=\"ja\""))

    def test_prefixed_opf_root_and_insertion(self):
        import re
        source = re.sub(r'<(/?)(package|metadata|meta|manifest|item|spine|itemref)(?=[\s/>])',
                        r'<\1o:\2', SOURCE).replace('xmlns="http://www.idpf.org/2007/opf"',
                                                   'xmlns:o="http://www.idpf.org/2007/opf"')
        self.assertEqual(self.apply(self.before.replace('a.xhtml', 'renamed.xhtml'), source),
                         source.replace('a.xhtml', 'renamed.xhtml'))
        item = "<item id='c' href='c.xhtml' media-type='application/xhtml+xml'/>"
        result = self.apply(self.before.replace('</manifest>', item + '</manifest>'), source)
        document = Document(result)
        manifest = next(n for n in document.root.children if n.name.endswith('|manifest'))
        self.assertEqual(len(manifest.children), 3)
        self.assertTrue(all(n.name == 'http://www.idpf.org/2007/opf|item' for n in manifest.children))

    def test_cdata_and_comments_in_changed_metadata(self):
        source = SOURCE.replace('A &amp; B', '<![CDATA[A & B]]>')
        result = self.apply(self.before.replace('A &amp; B', 'new]]&gt;value'), source)
        self.assertEqual(result, source.replace('<![CDATA[A & B]]>', '<![CDATA[new]]]]><![CDATA[>value]]>'))
        source = SOURCE.replace('A &amp; B', 'A<!--keep--> &amp; B')
        result = self.apply(self.before.replace('A &amp; B', 'new'), source)
        self.assertEqual(result, source.replace('A<!--keep--> &amp; B', 'new<!--keep-->'))

    def test_self_closing_metadata_can_receive_text(self):
        source = SOURCE.replace("<dc:title id='title'>A &amp; B</dc:title>", "<dc:title id='title'/>")
        self.assertEqual(self.apply(self.before, source, model(source)),
                         source.replace("<dc:title id='title'/>", "<dc:title id='title'>A &amp; B</dc:title>"))

    def test_unique_title_can_gain_id_without_reserializing(self):
        source = SOURCE.replace(" id='title'", '')
        before = model(source)
        after = before.replace('<dc:title>', '<dc:title id="new">')
        self.assertEqual(self.apply(after, source, before), source.replace('<dc:title>', '<dc:title id="new">'))

    def test_repeated_creators_reorder_by_content(self):
        extra = '<dc:creator>A</dc:creator><!--authors--><dc:creator>B</dc:creator>'
        source = SOURCE.replace('</metadata>', extra + '</metadata>')
        before = model(source)
        after = before.replace('<dc:creator>A</dc:creator><dc:creator>B</dc:creator>',
                               '<dc:creator>B</dc:creator><dc:creator>A</dc:creator>')
        self.assertEqual(self.apply(after, source, before), source.replace(extra,
                         '<dc:creator>B</dc:creator><!--authors--><dc:creator>A</dc:creator>'))

    def test_reject_malformed_output_and_unmappable_custom_entities(self):
        with self.assertRaises(ValueError):
            self.apply(self.before.replace('A &amp; B', '<broken>'))
        source = SOURCE.replace("<?publisher keep=\"this\"?>", '<!DOCTYPE package [<!ENTITY e "value">]>')
        with self.assertRaises(ValueError):
            self.apply(self.before.replace('a.xhtml', 'new.xhtml'), source)

    def test_legacy_parser_roundtrip(self):
        # Use the real existing parser pipeline, not only synthetic models.
        from xmlprocessor import repairXML
        from opf_newparser import Opf_Parser
        before = Opf_Parser(repairXML(SOURCE, 'application/oebps-package+xml')).rebuild_opfxml()
        after = before.replace('a.xhtml', 'renamed.xhtml')
        self.assertNotEqual(before, after)
        self.assertEqual(self.apply(after, SOURCE, before), SOURCE.replace('a.xhtml', 'renamed.xhtml'))

    def test_native_model_projection_excludes_nested_extensions(self):
        from opf_newparser import Opf_Parser
        before = Opf_Parser(model_xml(SOURCE)).rebuild_opfxml()
        self.assertNotIn('x:child', before)
        self.assertNotIn('x:extra', before)
        after = before.replace('A &amp; B', 'Changed')
        self.assertEqual(self.apply(after, SOURCE, before), SOURCE.replace('A &amp; B', 'Changed'))

    def test_model_projection_checks_namespaces_and_direct_children(self):
        source = SOURCE.replace('</manifest>', "<x:item id='fake' href='fake.xhtml'/>"
                                "<x:extension><item id='nested' href='nested.xhtml'/></x:extension></manifest>")
        before = model_xml(source)
        self.assertNotIn('fake.xhtml', before)
        self.assertNotIn('nested.xhtml', before)
        self.assertEqual(self.apply(before.replace('a.xhtml', 'renamed.xhtml'), source, before),
                         source.replace('a.xhtml', 'renamed.xhtml'))

    def test_model_projection_canonicalizes_opf_and_dc_prefixes(self):
        import re
        source = re.sub(r'<(/?)(package|metadata|meta|manifest|item|spine|itemref)(?=[\s/>])',
                        r'<\1p:\2', SOURCE).replace('xmlns="http://www.idpf.org/2007/opf"',
                                                   'xmlns:p="http://www.idpf.org/2007/opf"')
        source = source.replace('dc:', 'd:').replace('xmlns:dc=', 'xmlns:d=')
        before = model_xml(source)
        self.assertIn('<package ', before)
        self.assertIn('<dc:title ', before)
        self.assertEqual(self.apply(before.replace('A &amp; B', 'Changed'), source, before),
                         source.replace('A &amp; B', 'Changed'))

    def test_model_projection_rejects_external_entities(self):
        source = SOURCE.replace("<?publisher keep=\"this\"?>",
                                '<!DOCTYPE package [<!ENTITY external SYSTEM "file:///nonexistent-opf-entity">]>')
        source = source.replace('A &amp; B', '&external;')
        with self.assertRaises(Exception):
            model_xml(source)

    def test_navigation_preview_only_adds_one_manifest_item(self):
        result = add_navigation_manifest(SOURCE, 'nav.xhtml', 'nav')
        import re
        restored, count = re.subn(r'\r\n[^\S\r\n]*<item\b[^>]*properties="nav"[^>]*/>', '', result)
        self.assertEqual(count, 1)
        self.assertEqual(restored, SOURCE)
        for href, identifier in (('a.xhtml', 'new'), ('nav.xhtml', 'a')):
            with self.assertRaises(ValueError):
                add_navigation_manifest(SOURCE, href, identifier)
        with self.assertRaises(ValueError):
            add_navigation_manifest(result, 'nav2.xhtml', 'nav2')

    def test_multiple_new_attributes_share_one_namespace_declaration(self):
        after = self.before.replace("<dc:title id='title'",
                                    "<dc:title id='title' xmlns:z='urn:new' z:a='1' z:b='2'")
        result = self.apply(after)
        self.assertEqual(result.count('xmlns:z='), 1)
        document = Document(result)
        title = document.root.children[0].children[1]
        self.assertEqual(title.attrs['urn:new|a'], '1')
        self.assertEqual(title.attrs['urn:new|b'], '2')

    def test_comment_syntax_inside_cdata_is_text(self):
        source = SOURCE.replace('A &amp; B', '<![CDATA[A <!--text--> B]]>')
        before = self.before.replace('A &amp; B', 'A &lt;!--text--&gt; B')
        after = before.replace('A &lt;!--text--&gt; B', 'new')
        self.assertEqual(self.apply(after, source, before),
                         source.replace('<![CDATA[A <!--text--> B]]>', '<![CDATA[new]]>'))

    def test_populate_empty_manifest_with_namespaced_item(self):
        source = '<package xmlns="http://www.idpf.org/2007/opf"><metadata/><manifest/><spine/></package>'
        after = source.replace('<manifest/>', '<manifest><item id="a" href="a.xhtml" media-type="application/xhtml+xml"/></manifest>')
        result = self.apply(after, source, source)
        parsed = Document(result)
        self.assertEqual(parsed.root.children[1].children[0].attrs['id'], 'a')
        self.assertIn('<metadata/>', result)
        self.assertIn('<spine/>', result)

    def test_large_manifest_changes_only_one_attribute(self):
        entries = ''.join('<item id="i{0}" href="chapter{0}.xhtml" media-type="application/xhtml+xml"/>\n'.format(i)
                          for i in range(12000))
        source = '<package xmlns="http://www.idpf.org/2007/opf"><metadata/><manifest>\n' + entries + '</manifest><spine/></package>'
        after = source.replace('chapter10987.xhtml', 'renamed.xhtml')
        self.assertEqual(self.apply(after, source, source), after)

    def test_carriage_return_value_survives_xml_normalization(self):
        after = self.before.replace('A &amp; B', 'A&#13;B')
        self.assertEqual(self.apply(after), SOURCE.replace('A &amp; B', 'A&#13;B'))
        source = SOURCE.replace('A &amp; B', '<![CDATA[A & B]]>')
        result = self.apply(after, source)
        title = Document(result).root.children[0].children[1]
        self.assertEqual(title.text, 'A\rB')


if __name__ == '__main__':
    unittest.main()
