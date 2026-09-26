"""Compare native XML well-formed checks with xmlprocessor."""

import subprocess
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
sys.path[:0] = [
    str(ROOT / "src/Resource_Files/python3lib"),
    str(ROOT / "src/Resource_Files/plugin_launchers/python"),
]
import xmlprocessor


CASES = [
    '<?xml version="1.0"?>\r\n<ncx><navMap><!-- keep --><content src="Text/a.xhtml#id"/></navMap></ncx>',
    '<smil><text src="a.xhtml#id"/></smil>',
    '<page-map><page href="a.xhtml#id"/></page-map>',
    "<?xml version='1.0'?><package xmlns='http://www.idpf.org/2007/opf' version='3.0'>"
    "<metadata/><manifest/><spine/></package>",
    '<package><metadata></package>',
    '<ncx><navMap><navPoint><content src="a"/></navMap></ncx>',
    '<smil><body><seq><par><text src="a"/></seq></body></smil>',
    '<page-map><page href="a"></page-map>',
    '',
    '<?xml version="1.0" encoding="utf-8"?>\n<broken>',
    '<root attr="ok">text</root>',
    '<root>&amp; &lt; &#65;</root>',
    '<root>\u4e00本</root>',
]


def main():
    payload = ''.join(source.encode().hex() + '\n' for source in CASES)
    output = subprocess.run([sys.argv[1]], input=payload, text=True, capture_output=True, check=True).stdout.splitlines()
    if len(output) != len(CASES):
        raise SystemExit(f"expected {len(CASES)} results, got {len(output)}")
    for source, line in zip(CASES, output):
        expected = xmlprocessor.WellFormedXMLErrorCheck(source)
        column_parts = line.split('\t', 2)
        actual = [column_parts[0], column_parts[1], bytes.fromhex(column_parts[2]).decode()]
        if actual != expected:
            raise SystemExit(f"differs for {source!r}:\n actual {actual}\n expect {expected}")
    print(f"{len(CASES)} XML well-formed checks match legacy Python")


if __name__ == "__main__":
    main()
