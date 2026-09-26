"""Run remaining OPF acceptance paths through the real MainWindow."""

import argparse
import pathlib
import xml.etree.ElementTree as ET
import zipfile

from run_main_window_noop_integration import epub2_fixture
from run_opf_resource_integration import epub_fixture, run


def local_name(tag):
    return tag.rsplit("}", 1)[-1]


def verify_rename(path, _source, members):
    output = pathlib.Path(str(path) + ".rename.epub.renamed.epub")
    with zipfile.ZipFile(output) as archive:
        names = set(archive.namelist())
        assert "OEBPS/renamed.xhtml" in names, "renamed chapter is absent from output"
        assert "OEBPS/a.xhtml" not in names, "old chapter path remains in output"
        opf = ET.fromstring(archive.read("OEBPS/content.opf"))
        manifest_hrefs = {
            node.attrib.get("href")
            for node in opf.iter()
            if local_name(node.tag) == "item"
        }
        assert "renamed.xhtml" in manifest_hrefs, "manifest href was not renamed"
        assert "a.xhtml" not in manifest_hrefs, "old manifest href remains"
        navigation_name = (
            "OEBPS/nav.xhtml"
            if "OEBPS/nav.xhtml" in names
            else "OEBPS/toc.ncx"
        )
        navigation = archive.read(navigation_name).decode("utf-8")
        assert "renamed.xhtml" in navigation, (
            "navigation target was not renamed: " + navigation
        )
        assert 'href="a.xhtml"' not in navigation
        assert "src='a.xhtml'" not in navigation
        assert archive.read("mimetype") == members["mimetype"], "unrelated mimetype changed"

    generated = pathlib.Path(str(path) + ".ncx.epub.generated.epub")
    if generated.exists():
        with zipfile.ZipFile(generated) as archive:
            names = set(archive.namelist())
            assert "OEBPS/toc.ncx" in names, "generated NCX is missing from EPUB"
            ncx = archive.read("OEBPS/toc.ncx").decode("utf-8")
            assert '<content src="a.xhtml" />' in ncx
            opf = ET.fromstring(archive.read("OEBPS/content.opf"))
            items = [node for node in opf.iter() if local_name(node.tag) == "item"]
            assert any(node.attrib.get("href") == "toc.ncx" for node in items), (
                "generated NCX is absent from the OPF manifest"
            )


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("build", type=pathlib.Path)
    parser.add_argument("--fixture", choices=("epub2", "epub3"), default="epub3")
    args = parser.parse_args()
    run(
        args.build.resolve(),
        "main_window_opf_completion_integration_test.cpp",
        epub2_fixture if args.fixture == "epub2" else epub_fixture,
        verify_rename,
        direct_app_executable=True,
        run_arguments=[
            ("rename",),
            ("undo",),
            ("conflict",),
            ("write-failure",),
        ] + ([("ncx",)] if args.fixture == "epub3" else []),
    )
