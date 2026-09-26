"""Compare the C++ default replace functions with the legacy Python modules."""

import json
import random
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
sys.path[:0] = [str(ROOT / "src/Resource_Files/python3lib")]
import html
from fr_utils import capitalize, swapcase, toLower, toUpper
from functionsearch import _CaseChgFunctions, _EMPTY_FUNCTION, getFunctionSearchEnv
from titlecase import titlecase

PROBE = None
NAMES = list(_CaseChgFunctions)
BUILTIN_INDEX = {name: index + 1 for index, name in enumerate(NAMES)}


def hexed(text):
    return text.encode("utf-8").hex()


def run(requests):
    payload = "".join("\t".join([operation] + [hexed(field) for field in fields]) + "\n" for operation, *fields in requests)
    output = subprocess.run([PROBE], input=payload, capture_output=True, text=True, check=True).stdout.splitlines()
    if len(output) != len(requests):
        raise AssertionError("probe answered %d of %d requests" % (len(output), len(requests)))
    return [bytes.fromhex(line).decode("utf-8") for line in output]


def utf16(text, index):
    return len(text[:index].encode("utf-16-le")) // 2 if index >= 0 else index


class ReplaceFunctionsParityTest(unittest.TestCase):
    maxDiff = 2000

    def compare(self, operation, legacy, inputs):
        results = run([(operation, value) for value in inputs])
        failures = [(value, legacy(value), result) for value, result in zip(inputs, results) if legacy(value) != result]
        self.assertEqual(failures[:5], [], "%d of %d %s cases differ" % (len(failures), len(inputs), operation))

    def test_every_codepoint(self):
        characters = [chr(value) for value in range(0x110000) if not 0xD800 <= value <= 0xDFFF]
        for operation, legacy in (("U", toUpper), ("L", toLower), ("S", swapcase), ("C", capitalize), ("T", titlecase)):
            with self.subTest(operation=operation):
                self.compare(operation, legacy, characters)

    def test_final_sigma_and_case_contexts(self):
        pool = ["A", "a", "\u03a3", "\u03c2", "\u03c3", "\u02b0", "'", "\u0301", "\u00ad", " ", "1", ".", "\u0130",
                "\u00df", "\u01c5", "\ufb01", "\U00010400", "\u1e9e", "\u2126", "\u212a"]
        generator = random.Random(1)
        inputs = sorted({"".join(generator.choice(pool) for _ in range(generator.randrange(1, 7))) for _ in range(30000)})
        for operation, legacy in (("U", toUpper), ("L", toLower), ("S", swapcase), ("C", capitalize), ("T", titlecase)):
            with self.subTest(operation=operation):
                self.compare(operation, legacy, inputs)

    def test_titlecase_phrases(self):
        tokens = ["a", "an", "and", "as", "at", "but", "by", "en", "for", "if", "in", "of", "on", "or", "the", "to",
                  "v", "v.", "via", "vs", "vs.", "IN", "A", "The", "AN", "d'artagnan", "O'Neil", "l\u2018ami",
                  "D'x", "e.g.", "U.S.", "A.B", "A.B.C", "iPhone", "HTML", "mcDonald", "self-help", "x-ray", "-a-",
                  "--", "\u2014", "'quote'", '"dq"', "(paren)", "[x]", "_under", "_a", "3", "42", "1st", "Stra\u00dfe",
                  "\u01c6", "\ufb01ne", "\u01c5", "\u03a3\u03af\u03c3\u03c5\u03c6\u03bf\u03c2", "\u03a3\u0391\u03a3",
                  "istanbul", "\u0130stanbul", "\u0131n", "\u017ftar", "\u212aelvin", "a:", "end.", "?", "the,",
                  "\u2018tis", "\u00e9t\u00e9", "\u4e2d\u6587", "caf\u00e9", "o'", "'", "x.y", "\u0664\u0662",
                  "vs.x", "v.v.", "a.", "AN.", "\u0661"]
        separators = [" ", " ", " ", "  ", "\t", "\n", "\u00a0", "\u3000", "\x1c", "\x85", ": ", ". ", "? ", "! ", "; "]
        generator = random.Random(2)
        inputs = set()
        for _ in range(40000):
            words = [generator.choice(tokens) for _ in range(generator.randrange(1, 7))]
            text = "".join(word + generator.choice(separators) for word in words[:-1]) + words[-1]
            if generator.random() < 0.2:
                text = toUpper(text)
            if generator.random() < 0.1:
                text += "\n"
            if generator.random() < 0.1:
                text = generator.choice(separators) + text
            inputs.add(text)
        self.compare("T", titlecase, sorted(inputs))

    def test_html_unescape(self):
        pieces = ["&", "&amp", "&amp;", "&ampx;", "&#", "&#65", "&#65;", "&#x41;", "&#X41", "&#xZZ", "&#128;",
                  "&#0;", "&#13;", "&#55296;", "&#1114112;", "&#99999999999999999999;", "&#x110000;", "&#1;",
                  "&#xfffe;", "&#x1F600", "&notit;", "&notin;", "&not", "&Aacute", "&Aacutex", "&" + "a" * 40,
                  "&;", "& ", "&#;", "&nbsp;", "&lt", "&gt;", "&#0x41;", "&x", "&\t", "&&", "text", " ", "\u00e9",
                  "\U0001f600", ";", "#"]
        generator = random.Random(3)
        inputs = {"".join(generator.choice(pieces) for _ in range(generator.randrange(1, 6))) for _ in range(30000)}
        for name in html.entities.html5:
            inputs.update({"&" + name, "&" + name + "x", "&" + name.rstrip(";") + "q;"})
        self.compare("E", html.unescape, sorted(inputs))

    def test_single_replacement_matches_legacy(self):
        texts = ["<em class=\"Keep\">hello &amp; WORLD</em> Stra\u00dfe", "\U0001f600<em>x\u00df</em>",
                 "a <b>bold \u03a3</b> &lt;c&gt; d", "<>< x > <unclosed \u0130", "the lord of the rings: a tale",
                 "\U0001f600\U0001f601 &nbsp; \u01c5ungla &#x1F600; end", "\u03a3\u03a3 <i>\u03a3</i> \u03a3."]
        generator = random.Random(4)
        cases = []
        for text in texts:
            length = len(text)
            for _ in range(80):
                groups = [(0, length)]
                for _ in range(generator.randrange(0, 4)):
                    if generator.random() < 0.2:
                        groups.append((-1, -1))
                        continue
                    start = generator.randrange(0, length + 1)
                    groups.append((start, generator.randrange(start, length + 1)))
                cases.append((text, groups))
        requests, expected = [], []
        for name in NAMES + ["not_defined"]:
            for text, groups in cases:
                search = getFunctionSearchEnv("<metadata/>", name)
                expected.append(search.get_single_replacement_by_function("OEBPS/a.xhtml", text, groups))
                encoded = ";".join("%d,%d" % (utf16(text, start), utf16(text, end)) for start, end in groups)
                requests.append(("A", name, text, encoded))
        results = run(requests)
        failures = [(request[1:], want, got) for request, want, got in zip(requests, expected, results) if want != got]
        self.assertEqual(failures[:5], [], "%d of %d single replacements differ" % (len(failures), len(requests)))

    def test_resolution_of_the_user_json(self):
        changed = dict(_CaseChgFunctions)
        changed["uppercase"] = _CaseChgFunctions["uppercase"] + "# edited\n"
        swapped = dict(_CaseChgFunctions, uppercase=_CaseChgFunctions["lowercase"])
        custom = dict(_CaseChgFunctions, shout=_CaseChgFunctions["uppercase"], same=_EMPTY_FUNCTION,
                      mine="def replace(match, number, file_name, metadata, data):\n\treturn 'x'\n", odd=7)
        cases = [
            (None, "uppercase", BUILTIN_INDEX["uppercase"]),
            ("{}", "titlecase", BUILTIN_INDEX["titlecase"]),
            ("not json", "swapcase", BUILTIN_INDEX["swapcase"]),
            ("\ufeff" + json.dumps(changed), "uppercase", BUILTIN_INDEX["uppercase"]),
            ("[]", "lowercase", BUILTIN_INDEX["lowercase"]),
            ('["uppercase"]', "uppercase", 0),
            ("{}", "missing", 0),
            (json.dumps(changed), "uppercase", "python"),
            (json.dumps(changed), "lowercase", BUILTIN_INDEX["lowercase"]),
            (json.dumps(changed), "missing", 0),
            (json.dumps(swapped), "uppercase", BUILTIN_INDEX["lowercase"]),
            (json.dumps(custom), "shout", BUILTIN_INDEX["uppercase"]),
            (json.dumps(custom), "same", 0),
            (json.dumps(custom), "mine", "python"),
            (json.dumps(custom), "odd", 0),
        ]
        sample = "<p>the &amp; \u03a3a</p>"
        with tempfile.TemporaryDirectory() as directory:
            requests, expected = [], []
            for index, (content, name, want) in enumerate(cases):
                path = Path(directory) / ("functions-%d.json" % index)
                if content is not None:
                    path.write_text(content, encoding="utf-8")
                requests.append(("R", name, str(path)))
                expected.append(str(want))
                if want != "python":
                    try:
                        search = getFunctionSearchEnv("", name, str(path))
                        legacy = search.get_single_replacement_by_function("", sample, [(0, len(sample))])
                    except Exception:
                        # The application then has no search environment and keeps the text.
                        legacy = sample
                    builtin = name if want else "not_defined"
                    if want and (name not in _CaseChgFunctions or BUILTIN_INDEX[name] != want):
                        builtin = [key for key, value in BUILTIN_INDEX.items() if value == want][0]
                    self.assertEqual(run([("A", builtin, sample, "0,%d" % len(sample))])[0], legacy, (content, name))
            self.assertEqual(run(requests), expected)


if __name__ == "__main__":
    PROBE = sys.argv[1]
    unittest.main(argv=[sys.argv[0]])
