"""Frozen results from the legacy importtxt.read_unicode decoder."""

import sys
import tempfile
import unittest
from pathlib import Path


sys.path.insert(0, str(Path(__file__).resolve().parent / "fixtures"))
from importtxt_legacy import read_unicode


class ImportTxtGoldenTest(unittest.TestCase):
    def test_strict_fallback_order_and_boms(self):
        cases = (
            ("utf8", bytes.fromhex("e4b8ade69687"), "中文"),
            ("utf8_bom", bytes.fromhex("efbbbf48656c6c6f0d0a"), "\ufeffHello\r\n"),
            ("gb18030", bytes.fromhex("d6d0cec4"), "中文"),
            ("gb18030_four_byte", bytes.fromhex("9439fc36"), "😀"),
            ("gb18030_first_supplementary", bytes.fromhex("90308130"), "𐀀"),
            ("gb18030_last_scalar", bytes.fromhex("e3329a35"), "\U0010ffff"),
            ("gb18030_outside_unicode", bytes.fromhex("e3329a36"), "㋣㚚"),
            ("utf16_le", bytes.fromhex("fffe48006900"), "Hi"),
            ("utf16_be", bytes.fromhex("feff00480069"), "Hi"),
            ("nul_utf8", bytes.fromhex("48006900"), "H\x00i\x00"),
            ("bomless_utf16_fallback", bytes.fromhex("8130"), "め"),
            ("bomless_utf16_fallback_2", bytes.fromhex("81ff"), "ﾁ"),
            ("truncated_utf8_then_utf16", bytes.fromhex("00c2"), "숀"),
            ("truncated_gb18030", bytes.fromhex("81"), ""),
            ("invalid", bytes.fromhex("ff"), ""),
            ("truncated_utf16", bytes.fromhex("fffe48"), ""),
            ("empty", b"", ""),
        )
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "input.txt"
            for name, data, expected in cases:
                with self.subTest(name=name):
                    path.write_bytes(data)
                    self.assertEqual(read_unicode(str(path)), expected)


if __name__ == "__main__":
    unittest.main()
