"""Compare manifest id rebasing with the legacy Python routine."""

import subprocess
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
sys.path[:0] = [
    str(ROOT / "tests"),
    str(ROOT / "tests/fixtures"),
    str(ROOT / "src/Resource_Files/plugin_launchers/python"),
]
from fix_opf_ids_legacy import rebase_manifest_ids
from opf_id_rebase_legacy_golden_test import EXPECTED, SOURCE


def native(program, operation, source):
    payload = operation + "\t" + source.encode().hex() + "\n"
    output = subprocess.run([program], input=payload, text=True, capture_output=True, check=True).stdout.strip()
    if output == "E":
        raise AssertionError(operation + " native rebase failed")
    if not output.startswith("S"):
        raise AssertionError(output)
    return bytes.fromhex(output[1:]).decode()


def main():
    program = sys.argv[1]
    cases = [
        SOURCE,
        SOURCE.replace("<!-- keep comment -->\n  ", ""),
        SOURCE.replace('id="audio-old"', 'id="narration_mp3"'),
        '<?xml version="1.0"?>\n<package version="2.0" xmlns="http://www.idpf.org/2007/opf">'
        '<metadata><dc:identifier id="bookid">x</dc:identifier></metadata>'
        '<manifest><item id="c" href="Text/1.xhtml" media-type="application/xhtml+xml"/></manifest>'
        '<spine><itemref idref="c"/></spine></package>',
    ]
    for source in cases:
        actual = native(program, "L", source)
        expected = rebase_manifest_ids(source)
        if actual != expected:
            raise SystemExit("legacy rebase differs:\n" + actual + "\n---\n" + expected)
    if native(program, "L", SOURCE) != EXPECTED:
        raise SystemExit("golden legacy rebase differs")

    preserved = native(program, "P", SOURCE)
    if "<!-- keep comment -->" not in preserved:
        raise SystemExit("preserving rebase dropped a comment")
    if "Images/%E5%B0%81%E9%9D%A2.jpg" not in preserved:
        raise SystemExit("preserving rebase decoded an href")
    for old, new in (
        ("cover-old", "FengMian_jpg"),
        ("chapter-old", "DiYiZhang_xhtml"),
        ("second-old", "DiYiZhang_xhtml0001"),
        ("audio-old", "narration_mp3"),
        ("ncx-old", "toc_ncx"),
    ):
        if old in preserved or new not in preserved:
            raise SystemExit(f"preserving rebase did not retarget {old} -> {new}")
    narrative = SOURCE.replace("</metadata>", "<dc:title>chapter-old</dc:title></metadata>")
    preserved_text = native(program, "P", narrative)
    if "<dc:title>chapter-old</dc:title>" not in preserved_text:
        raise SystemExit("preserving rebase rewrote narrative text")
    print(f"{len(cases)} legacy manifest id rebases match; source-preserving rebase keeps unrelated text")


if __name__ == "__main__":
    main()
