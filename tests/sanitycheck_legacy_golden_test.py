"""Freeze the validation panel's legacy HTML sanity-check messages."""

import sys
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tests/fixtures"))
from sanitycheck_legacy import perform_sanity_check


VALID = (
    '<?xml version="1.0" encoding="utf-8"?>\n'
    '<html><head><title>Title</title></head>'
    '<body><p>你好 &amp; hi</p><br/></body></html>'
)


class SanityCheckLegacyGoldenTest(unittest.TestCase):
    def test_validation_results(self):
        cases = {
            "valid": (VALID, []),
            "missing_header": (
                '<html><head></head><body></body></html>',
                ['error\x1fmissing_header.xhtml\x1f1\x1f-1\x1f'
                 'Missing or multiple "xml declaration header".  near column 0'],
            ),
            "bad_nesting": (
                VALID.replace('</p>', '</body>'),
                ['error\x1fbad_nesting.xhtml\x1f2\x1f-1\x1f'
                 'Improperly nested tags: parsing end tag "body" but current parse '
                 'path is "None.html.body.p". See line 2 col 66.  near column 59'],
            ),
            "void_close": (
                VALID.replace('<br/>', '<br></br>'),
                ['error\x1fvoid_close.xhtml\x1f2\x1f-1\x1f'
                 'Void tag: br has an illegal ending tag.  near column 67'],
            ),
            "missing_structure": (
                '<?xml version="1.0"?><root/>',
                ['error\x1fmissing_structure.xhtml\x1f1\x1f-1\x1f'
                 'Missing or multiple "html" tags.  near column 0',
                 'error\x1fmissing_structure.xhtml\x1f1\x1f-1\x1f'
                 'Missing or multiple "body" tags.  near column 0',
                 'error\x1fmissing_structure.xhtml\x1f1\x1f-1\x1f'
                 'Missing or multiple "head" tags.  near column 0'],
            ),
            "duplicate_head": (
                VALID.replace('<body>', '<head></head><body>'),
                ['error\x1fduplicate_head.xhtml\x1f1\x1f-1\x1f'
                 'Missing or multiple "head" tags.  near column 0'],
            ),
            # The existing checker accepts this missing space and bare ampersand.
            "permissive": (
                VALID.replace('<p>', '<p class="x"id="a">').replace(' &amp; hi', ' & hi'),
                [],
            ),
        }
        with tempfile.TemporaryDirectory() as directory:
            for name, (source, expected) in cases.items():
                with self.subTest(name=name):
                    path = Path(directory) / f"{name}.xhtml"
                    path.write_text(source, encoding="utf-8")
                    self.assertEqual(perform_sanity_check(str(path)), expected)


if __name__ == "__main__":
    unittest.main()
