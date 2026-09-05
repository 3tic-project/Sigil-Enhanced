import codecs
import pathlib
import sys
import unittest

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1] / 'src/Resource_Files/python3lib'))
from opf_source_bytes import decode_source, encode_source, editor_text, restore_source_text


SOURCE = "<?xml version='1.0' encoding='UTF-8'?>\r\n<package>\n<!--保留 e\u0301 𠮷-->\r<title>original</title>\r\n</package>"


class SourceBytesTest(unittest.TestCase):
    def test_utf8_bom_mixed_line_endings_and_nfd(self):
        for bom in (b'', codecs.BOM_UTF8):
            data = bom + SOURCE.encode('utf-8')
            decoded = decode_source(data)
            self.assertEqual(decoded, SOURCE)
            self.assertEqual(encode_source(data, restore_source_text(decoded, editor_text(decoded))), data)
            changed = restore_source_text(decoded, editor_text(decoded).replace('original', 'changed'))
            self.assertEqual(encode_source(data, changed), data.replace(b'original', b'changed'))

    def test_utf16_utf32_roundtrips_and_edits(self):
        for encoding, bom, declaration in (
            ('utf-16-le', codecs.BOM_UTF16_LE, 'UTF-16'),
            ('utf-16-be', codecs.BOM_UTF16_BE, 'UTF-16'),
            ('utf-32-le', codecs.BOM_UTF32_LE, 'UTF-32'),
            ('utf-32-be', codecs.BOM_UTF32_BE, 'UTF-32')):
            with self.subTest(encoding=encoding):
                source = SOURCE.replace('UTF-8', declaration)
                data = bom + source.encode(encoding)
                self.assertEqual(decode_source(data), source)
                changed = restore_source_text(source, editor_text(source).replace('original', 'changed'))
                self.assertEqual(encode_source(data, changed), bom + source.replace('original', 'changed').encode(encoding))

    def test_no_bom_utf16_is_detected_from_xml_signature(self):
        for encoding in ('utf-16-le', 'utf-16-be'):
            source = SOURCE.replace('UTF-8', encoding)
            data = source.encode(encoding)
            self.assertEqual(decode_source(data), source)
            self.assertEqual(encode_source(data, source), data)

    def test_unrepresentable_characters_require_explicit_conversion(self):
        source = "<?xml version='1.0' encoding='ISO-8859-1'?><package>café</package>"
        data = source.encode('iso-8859-1')
        self.assertEqual(decode_source(data), source)
        changed = source.replace('café', '日本')
        with self.assertRaisesRegex(ValueError, 'UTF-8'):
            encode_source(data, changed)
        changed = changed.replace('ISO-8859-1', 'UTF-8')
        self.assertEqual(encode_source(data, changed), changed.encode('utf-8'))

    def test_removing_legacy_encoding_declaration_uses_xml_default(self):
        source = "<?xml version='1.0' encoding='ISO-8859-1'?><package>café</package>"
        data = source.encode('iso-8859-1')
        changed = source.replace(" encoding='ISO-8859-1'", '')
        self.assertEqual(encode_source(data, changed), changed.encode('utf-8'))

    def test_edited_lines_keep_individual_terminators(self):
        source = 'one\r\ntwo\nthree\rfour\u2028five\u2029six'
        edited = editor_text(source).replace('one', 'changed').replace('five', 'changed')
        self.assertEqual(restore_source_text(source, edited), source.replace('one', 'changed').replace('five', 'changed'))

    def test_line_insert_delete_and_empty_source(self):
        source = 'one\r\ntwo\r\nthree\r\n'
        self.assertEqual(restore_source_text(source, 'one\nnew\ntwo\nthree\n'),
                         'one\r\nnew\r\ntwo\r\nthree\r\n')
        self.assertEqual(restore_source_text(source, 'one\nthree\n'), 'one\r\nthree\r\n')
        self.assertEqual(restore_source_text('', 'new\n'), 'new\n')
        self.assertEqual(restore_source_text(source, ''), '')

    def test_bad_encoding_is_rejected(self):
        for data in (b'\xff', codecs.BOM_UTF8 + b"<?xml encoding='UTF-16'?><package/>",
                     b"<?xml encoding='no-such-codec'?><package/>"):
            with self.subTest(data=data), self.assertRaises((ValueError, LookupError)):
                decode_source(data)

    def test_repetitive_source_keeps_newlines(self):
        source = '<x/>\r\n' * 12000
        edited = editor_text(source)
        edited = edited[:24000] + '<y/>' + edited[24004:]
        self.assertEqual(editor_text(restore_source_text(source, edited)), edited)
        self.assertEqual(restore_source_text(source, edited).count('\r\n'), 12000)

    def test_insert_among_repeated_lines_preserves_mixed_ending_suffix(self):
        source = '<x/>\r\n' * 300 + '<x/>\n' * 300 + '<x/>\r\n' * 300
        edited = editor_text(source)
        edited = edited[:1500] + '<new/>\n' + edited[1500:]
        self.assertEqual(restore_source_text(source, edited),
                         '<x/>\r\n' * 300 + '<new/>\n' + '<x/>\n' * 300 + '<x/>\r\n' * 300)


if __name__ == '__main__':
    unittest.main()
