"""Exercise Add Copy followed immediately by inline XHTML renaming."""
import argparse
import pathlib
import zipfile
from xml.etree import ElementTree as ET

from run_opf_resource_integration import epub_fixture, run

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("build", type=pathlib.Path)
    parser.add_argument("--scenario", choices=("immediate", "loaded", "edited", "observed", "all"), default="all")
    parser.add_argument("--timeout", type=int, default=25)
    args = parser.parse_args()
    scenarios = ("immediate", "loaded", "edited", "observed") if args.scenario == "all" else (args.scenario,)

    def verify(path, _source, _members):
        for scenario in scenarios:
            with zipfile.ZipFile(str(path) + "." + scenario + ".epub") as archive:
                package = ET.fromstring(archive.read("OEBPS/content.opf"))
                ns = {"opf": "http://www.idpf.org/2007/opf"}
                manifest = {item.attrib["id"]: item.attrib["href"]
                            for item in package.findall("opf:manifest/opf:item", ns)}
                spine = [manifest[item.attrib["idref"]] for item in package.findall("opf:spine/opf:itemref", ns)]
                assert spine == ["a.xhtml", "copy-2.xhtml", "copy-1.xhtml", "copy-0.xhtml"], spine
                for index in range(3):
                    body = archive.read(f"OEBPS/copy-{index}.xhtml")
                    ET.fromstring(body)
                    assert b"Original paragraph" in body
                    assert (b"edited before rename" in body) == (scenario == "edited")
                assert b"edited before rename" not in archive.read("OEBPS/a.xhtml")
        print("Exported copy contents, manifest and spine verified")

    run(args.build.resolve(), "book_browser_copy_rename_integration_test.cpp", epub_fixture,
        verify, direct_app_executable=True,
        run_arguments=[(scenario,) for scenario in scenarios], run_timeout=args.timeout)
