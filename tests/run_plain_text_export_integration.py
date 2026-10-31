"""Export EPUB 2/3 through the real Enhancement menu and live editor documents."""
import argparse
import pathlib
import shutil
import zipfile

from run_opf_resource_integration import epub_fixture, run
from run_main_window_noop_integration import epub2_fixture


def fixture(scratch, version):
    path, source, members = (epub_fixture if version == "epub3" else epub2_fixture)(scratch)
    encoding = "utf-16" if version == "epub3" else "utf-8"
    opf = members["OEBPS/content.opf"].decode(encoding)
    extra = "<item id='z' href='z.xhtml' media-type='application/xhtml+xml'/>"
    extra += "<item id='notes' href='notes.xhtml' media-type='application/xhtml+xml'/>"
    if version == "epub3":
        extra += "<item id='svg' href='chapter.svg' media-type='image/svg+xml'/>"
    opf = opf.replace("</manifest>", extra + "</manifest>")
    order = "<itemref idref='z'/>"
    if version == "epub3":
        order += "<itemref idref='svg'/>"
    opf = opf.replace("<itemref idref='a'/>", order + "<itemref idref='a'/>")
    members["OEBPS/content.opf"] = opf.encode(encoding)

    def html(body):
        return ("<?xml version='1.0' encoding='utf-8'?><!DOCTYPE html>"
                "<html xmlns='http://www.w3.org/1999/xhtml'><head><title>Not body text</title>"
                "</head><body>" + body + "</body></html>").encode()

    members["OEBPS/z.xhtml"] = html("<h1>第一章</h1><div>　原文<ruby>漢字<rt>かんじ</rt></ruby>"
                                  " &amp; 译文𠮷<br/>下一行</div>")
    members["OEBPS/a.xhtml"] = html("<h1>第二章</h1><p>待编辑正文</p>")
    members["OEBPS/notes.xhtml"] = html("<p>附录</p>")
    expected = "第一章\n　原文漢字 & 译文𠮷\n下一行\n\n"
    if version == "epub3":
        members["OEBPS/chapter.svg"] = b"<svg xmlns='http://www.w3.org/2000/svg'><text>SVG chapter</text></svg>"
        members["OEBPS/nav.xhtml"] = html("<nav xmlns:epub='http://www.idpf.org/2007/ops' epub:type='toc'>"
                                        "<h2>目录</h2><ol><li><a href='z.xhtml'>第一章</a></li></ol></nav>")
        expected += "SVG chapter\n\n"
    expected += "第二章\n待编辑正文\n\n"
    if version == "epub3":
        expected += "目录\n第一章\n\n"
    expected += "附录\n"
    pathlib.Path(str(path) + ".expected").write_text(expected, encoding="utf-8")
    with zipfile.ZipFile(path, "w") as archive:
        for name, data in members.items():
            archive.writestr(name, data)
    return path, source, members


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("build", type=pathlib.Path)
    parser.add_argument("--fixture", choices=("epub2", "epub3"), default="epub3")
    parser.add_argument("--epub", type=pathlib.Path, help="Smoke-test a disposable copy of a real EPUB")
    args = parser.parse_args()
    def real_fixture(scratch):
        path = scratch / "fixture.epub"
        shutil.copyfile(args.epub, path)
        return path, "", {}

    run(args.build.resolve(), "plain_text_export_integration_test.cpp",
        real_fixture if args.epub else lambda scratch: fixture(scratch, args.fixture), lambda *args: None,
        direct_app_executable=True, run_arguments=[("smoke",)] if args.epub else None,
        run_timeout=60 if args.epub else 30)
