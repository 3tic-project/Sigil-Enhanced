"""Verify redo persistence and Find/Replace restart with actual editor widgets."""

import argparse
import pathlib
import zipfile

from run_opf_resource_integration import epub_fixture, run


SCENARIOS = ("redo-html", "redo-css", "restart-original", "restart-plus", "restart-plus-presearch")


def fixture(scratch):
    path, source, members = epub_fixture(scratch)
    opf = members["OEBPS/content.opf"].decode("utf-16").replace(
        "</manifest>", "<item id='b' href='b.xhtml' media-type='application/xhtml+xml'/>"
        "<item id='css' href='style.css' media-type='text/css'/></manifest>")
    opf = opf.replace("<itemref idref='a'/>", "<itemref idref='a'/><itemref idref='b'/>")
    members["OEBPS/content.opf"] = opf.encode("utf-16")
    members["OEBPS/a.xhtml"] = members["OEBPS/a.xhtml"].replace(
        b"Original paragraph", b"Original paragraph needle one needle two needle three")
    members["OEBPS/b.xhtml"] = members["OEBPS/a.xhtml"].replace(
        b"Original paragraph needle one needle two needle three", b"needle four")
    members["OEBPS/style.css"] = b"p { color: red; }\n/* needle one needle two needle three */\n"
    with zipfile.ZipFile(path, "w") as archive:
        for name, data in members.items():
            archive.writestr(name, data)
    return path, source, members


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("build", type=pathlib.Path)
    parser.add_argument("--scenario", choices=SCENARIOS + ("all",), default="all")
    args = parser.parse_args()
    scenarios = SCENARIOS if args.scenario == "all" else (args.scenario,)

    def verify(path, _source, members):
        for scenario in scenarios:
            if not scenario.startswith("redo-"):
                continue
            target = pathlib.Path(str(path) + "." + scenario + ".epub")
            name = "OEBPS/a.xhtml" if scenario == "redo-html" else "OEBPS/style.css"
            before, after = (b"Original paragraph", b"Redo changed paragraph") if scenario == "redo-html" else (b"red", b"blue")
            with zipfile.ZipFile(target) as archive:
                assert archive.read(name) == members[name].replace(before, after), "Redo text was not saved in the EPUB"
                for unchanged, data in members.items():
                    if unchanged not in (name, "OEBPS/content.opf"):
                        assert archive.read(unchanged) == data, "Save changed unrelated content: " + unchanged
            print("Redone content persisted in EPUB:", scenario)

    run(args.build.resolve(), "editor_undo_search_integration_test.cpp", fixture, verify,
        direct_app_executable=True, run_arguments=[(scenario,) for scenario in scenarios])
