"""Compare the native NCX generator byte for byte with the retired Python code."""

import random
import subprocess
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
sys.path[:0] = [
    str(ROOT / "tests/fixtures"),
    str(ROOT / "src/Resource_Files/plugin_launchers/python"),
]
from ncxgenerator_legacy import generateNCX
from ncxgenerator_legacy_golden_test import NAV, EXPECTED_NCX


def cases():
    yield NAV, "OEBPS/nav.xhtml", "OEBPS/NCX", "Book &amp; Tea", "urn:book"
    yield "<html><body/></html>", "OEBPS/nav.xhtml", "OEBPS", "Title", "id"
    yield '<nav epub:type="toc"><ol><li><a href="https://example.org/a/b">External</a></li></ol></nav>', "nav.xhtml", "", "T", "i"
    yield '<nav epub:type="toc"><ol><li><a href="sub/book.xhtml"><span>Nested</span> text</a></li></ol></nav>', "OPS/nav.xhtml", "OPS", "标题", "urn:id"
    yield '<nav epub:type="toc"><ol><li><a href="文%20件.xhtml#章%201">中 文</a></li></ol></nav>', "OPS/nav.xhtml", "OPS/NCX", "标题", "urn:id"
    yield '<nav epub:type="page-list"><ol><li><a href="./a.xhtml#p1">1</a></li></ol></nav>', "OPS/nav.xhtml", "OPS", "", ""
    yield '<nav epub:type="toc"><ol><li><a href="#frag">First</a></li><li><a href="">Second</a></li></ol></nav>', "OPS/nav.xhtml", "OPS", "T", "id"
    yield '<nav epub:type="toc"><ol><li><a href="a/b/../c.xhtml">One</a></li></ol></nav>', "OPS/nav.xhtml", "OPS", "T", "id"
    yield '<nav epub:type="toc"><ol><li><a href="one.xhtml">One<!-- comment -->Two</a></li></ol></nav>', "OPS/nav.xhtml", "OPS", "T", "id"
    yield '<nav epub:type="toc"><ol><li><a href="one.xhtml">One<![CDATA[hidden]]>Two</a></li></ol></nav>', "OPS/nav.xhtml", "OPS", "T", "id"

    rng = random.Random(20260925)
    hrefs = ["one.xhtml", "Text/two%20words.xhtml#part%201", "#end", "./章节.xhtml", "https://example.org/a/b", "../a.xhtml", "a%26b.xhtml", ""]
    titles = ["One", "中 文", "A &amp; B", "<span>part</span> title", "é", ""]
    for _ in range(200):
        entries = []
        for __ in range(rng.randrange(1, 10)):
            href = rng.choice(hrefs)
            title = rng.choice(titles)
            entries.append(f'<li><a href="{href}">{title}</a></li>')
        nav = '<html><body><nav epub:type="toc"><ol>' + ''.join(entries) + '</ol></nav></body></html>'
        yield nav, rng.choice(["OPS/nav.xhtml", "nav.xhtml"]), rng.choice(["OPS", "OPS/NCX", ""]), "T", "id"


def main():
    samples = list(cases())
    payload = ''.join('\t'.join(value.encode().hex() for value in case) + '\n' for case in samples)
    result = subprocess.run([sys.argv[1]], input=payload, text=True, capture_output=True, check=True)
    outputs = result.stdout.splitlines()
    assert len(outputs) == len(samples), (len(outputs), len(samples), result.stderr)
    for index, (case, output) in enumerate(zip(samples, outputs)):
        expected = generateNCX(*case)
        actual = bytes.fromhex(output).decode()
        assert actual == expected, f"Case {index}:\ninput={case!r}\nexpected={expected!r}\nactual={actual!r}"
        if index == 0:
            assert actual == EXPECTED_NCX
    print(f"{len(samples)} NCX cases match the legacy Python implementation")


if __name__ == "__main__":
    main()
