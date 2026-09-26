"""Freeze Python replacement semantics before moving built-in functions to C++."""

import contextlib
import hashlib
import io
import json
import sys
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
sys.path[:0] = [
    str(ROOT / "src/Resource_Files/python3lib"),
    str(ROOT / "src/Resource_Files/plugin_launchers/python"),
]
from functionsearch import createJsonFile, getFunctionSearchEnv


FUNCTION_NAMES = [
    "uppercase", "lowercase", "capitalize", "titlecase", "swapcase",
    "uppercase_ignore_tags", "lowercase_ignore_tags",
    "capitalize_ignore_tags", "titlecase_ignore_tags", "swapcase_ignore_tags",
]


class FunctionSearchLegacyGoldenTest(unittest.TestCase):
    def test_ten_builtins_entities_tags_and_capture_groups(self):
        source = '<em class="Keep">hello &amp; WORLD</em> Straße'
        pattern = r'<em[^>]*>(hello &amp; WORLD)</em>'
        expected = {
            "uppercase": '<em class="Keep">HELLO & WORLD</em> Straße',
            "lowercase": '<em class="Keep">hello & world</em> Straße',
            "capitalize": '<em class="Keep">Hello & world</em> Straße',
            "titlecase": '<em class="Keep">Hello & WORLD</em> Straße',
            "swapcase": '<em class="Keep">HELLO & world</em> Straße',
            "uppercase_ignore_tags": '<em class="Keep">HELLO & WORLD</em> Straße',
            "lowercase_ignore_tags": '<em class="Keep">hello & world</em> Straße',
            "capitalize_ignore_tags": '<em class="Keep">Hello & world</em> Straße',
            "titlecase_ignore_tags": '<em class="Keep">Hello & WORLD</em> Straße',
            "swapcase_ignore_tags": '<em class="Keep">HELLO & world</em> Straße',
        }
        self.assertEqual(set(expected), set(FUNCTION_NAMES))
        for name in FUNCTION_NAMES:
            with self.subTest(name=name):
                search = getFunctionSearchEnv("<metadata/>", name)
                self.assertEqual(
                    search.do_text_replacements(pattern, "OEBPS/a.xhtml", source),
                    expected[name],
                )
                self.assertEqual(search.get_current_replacement_count(), 1)

        # Without groups, ordinary functions change tag attributes too; the
        # _ignore_tags variants leave them untouched while decoding entities.
        full_match = r'<em[^>]*>hello &amp; WORLD</em>'
        self.assertEqual(
            getFunctionSearchEnv("", "uppercase").do_text_replacements(
                full_match, "", source,
            ),
            '<EM CLASS="KEEP">HELLO & WORLD</EM> Straße',
        )
        self.assertEqual(
            getFunctionSearchEnv("", "uppercase_ignore_tags").do_text_replacements(
                full_match, "", source,
            ),
            '<em class="Keep">HELLO & WORLD</em> Straße',
        )
        self.assertEqual(
            getFunctionSearchEnv("", "uppercase").do_text_replacements(
                r'(x)?(Straße)', "", "Straße",
            ),
            "STRASSE",
        )

    def test_single_replacement_uses_codepoint_offsets(self):
        search = getFunctionSearchEnv("", "uppercase")
        text = '😀<em>xß</em>'
        self.assertEqual(
            search.get_single_replacement_by_function(
                "OEBPS/a.xhtml", text, [(0, len(text)), (5, 7), (-1, -1)],
            ),
            '😀<em>XSS</em>',
        )
        self.assertEqual(search.get_current_replacement_count(), 1)
        self.assertEqual(
            search.get_single_replacement_by_function("", text, [(-1, -1)]),
            "",
        )
        self.assertEqual(search.get_current_replacement_count(), 1)

    def test_default_json_and_same_name_user_override(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "replace_functions.json"
            self.assertEqual(createJsonFile(str(path)), 1)
            raw = path.read_text(encoding="utf-8")
            self.assertEqual(
                hashlib.sha256(path.read_bytes()).hexdigest(),
                "79335fe21dfce2f64589d4e2a152f83f60f60e669ef5ea4391453d325a125b6a",
            )
            self.assertTrue(raw.startswith('{\n  "uppercase": "def replace('))
            definitions = json.loads(raw)
            self.assertEqual(list(definitions), FUNCTION_NAMES)
            self.assertIn('replace_uppercase(match, number, file_name, metadata, data)',
                          definitions["uppercase"])

            definitions["uppercase"] = (
                'def replace(match, number, file_name, metadata, data):\n'
                '\tdata["seen"] = data.get("seen", 0) + 1\n'
                '\treturn f"{number}:{data[\'seen\']}:{file_name}:{metadata}:{match.group(0)}"\n'
            )
            path.write_text(json.dumps(definitions, ensure_ascii=False), encoding="utf-8")
            self.assertEqual(createJsonFile(str(path)), 0)
            self.assertEqual(json.loads(path.read_text(encoding="utf-8")), definitions)

            search = getFunctionSearchEnv("<m/>", "uppercase", str(path))
            self.assertEqual(
                search.do_text_replacements("^a", "one.xhtml", "a\nb"),
                "1:1:one.xhtml:<m/>:a\nb",
            )
            self.assertEqual(
                search.do_text_replacements("^a", "two.xhtml", "a\na"),
                "2:2:two.xhtml:<m/>:a\n3:3:two.xhtml:<m/>:a",
            )
            self.assertEqual(search.get_current_replacement_count(), 3)
            self.assertEqual(search.funcData, {"seen": 3})
            self.assertEqual(
                search.get_single_replacement_by_function(
                    "three.xhtml", "😀xß", [(0, 3)],
                ),
                "4:4:three.xhtml:<m/>:😀xß",
            )

    def test_replace_all_uses_python_regex_and_keeps_text_on_invalid_pattern(self):
        search = getFunctionSearchEnv("", "lowercase")
        self.assertEqual(
            search.do_text_replacements("^A", "a.xhtml", "A\nA"),
            "a\na",
        )
        self.assertEqual(search.get_current_replacement_count(), 2)
        output = io.StringIO()
        with contextlib.redirect_stdout(output):
            self.assertEqual(
                search.do_text_replacements(r'\K', "a.xhtml", "A"),
                "A",
            )
        self.assertIn("Python Function Replace re error", output.getvalue())
        self.assertEqual(search.get_current_replacement_count(), 2)


if __name__ == "__main__":
    unittest.main()
