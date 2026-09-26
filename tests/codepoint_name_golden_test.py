"""Freeze the legacy getcodepointname behavior before replacing its caller."""

import hashlib
import sys
import unicodedata
import unittest
from pathlib import Path


sys.path.insert(0, str(Path(__file__).resolve().parent / "fixtures"))
from getcodepointname_legacy import getname


XHTML_CHARS = (
    "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ"
    "1234567890!@#$%^&*()_-+={}[]:;\"'<>,.?/|\\…„”“’»«"
    "ąćęłńóśżźĄĆĘŁŃÓŚŻŹáàâäãåÁÀÂÄÃÅéèêëÉÈÊËíìîïÍÌÎÏ"
    "òôöõøÓÒÔÖÕØúùûüÚÙÛÜýÿÝŸçÇñÑšžŠŽđĐœæŒÆß"
)

CONTROL_NAMES = (
    "NULL", "START OF HEADING", "START OF TEXT", "END OF TEXT",
    "END OF TRANSMISSION", "ENQUIRY", "ACKNOWLEDGE", "BELL",
    "BACKSPACE", "TAB", "NEW LINE", "VERTICAL TAB", "FORM FEED",
    "CARRIAGE RETURN", "SHIFT OUT", "SHIFT IN", "DATA LINK ESCAPE",
    "DEVICE CONTROL ONE", "DEVICE CONTROL TWO", "DEVICE CONTROL THREE",
    "DEVICE CONTROL FOUR", "NEGATIVE ACKNOWLEDGE", "SYNCHRONOUS IDLE",
    "END OF TRANSMISSION BLOCK", "CANCEL", "END OF MEDIUM",
    "SUBSTITUTE", "ESCAPE", "FILE SEPARATOR (FS)",
    "GROUP SEPARATOR (GS)", "RECORD SEPARATOR (RS)", "UNIT SEPARATOR (US)",
)


class CodepointNameGoldenTest(unittest.TestCase):
    def test_control_names(self):
        self.assertEqual(tuple(getname(cp) for cp in range(32)), CONTROL_NAMES)

    def test_xhtml_cache_names(self):
        names = "\n".join(
            f"{cp}:{getname(cp)}" for cp in sorted({ord(char) for char in XHTML_CHARS})
        )
        self.assertEqual(len({ord(char) for char in XHTML_CHARS}), 182)
        self.assertEqual(
            hashlib.sha256(names.encode("utf-8")).hexdigest(),
            "40fdfbbf797eceed3fbfb9ff8c337d007bd7ee94482f941b2935ab143b278cbd",
        )

    def test_boundaries(self):
        self.assertEqual(getname(-2), "")
        self.assertEqual(getname(-1), "")  # C++ CodepointNames adds its EOF sentinel.
        self.assertEqual(getname(0x4E00), "CJK UNIFIED IDEOGRAPH-4E00")
        self.assertEqual(getname(0xAC00), "HANGUL SYLLABLE GA")
        self.assertEqual(getname(0x1F600), "GRINNING FACE")
        self.assertEqual(getname(0x1FAE8), "Unknown")  # Added after Unicode 14.
        self.assertEqual(getname(0x10FFFF), "Unknown")
        with self.assertRaises(ValueError):
            getname(0x110000)


if __name__ == "__main__":
    if unicodedata.unidata_version != "14.0.0":
        print("requires the legacy Python Unicode 14.0.0 database")
        sys.exit(77)
    unittest.main()
