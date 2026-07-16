#!/usr/bin/env python3
"""Report reusable EPUB structure and layout features without extracting content."""

import argparse
import json
import pathlib
import posixpath
import re
import sys
import urllib.parse
import zipfile
import xml.etree.ElementTree as ET


CONTAINER_NS = "urn:oasis:names:tc:opendocument:xmlns:container"
OPF_NS = "http://www.idpf.org/2007/opf"
DC_NS = "http://purl.org/dc/elements/1.1/"
IMAGE_SUFFIXES = {".gif", ".jpeg", ".jpg", ".png", ".svg", ".webp"}


def local_name(tag):
    return tag.rsplit("}", 1)[-1]


def text_values(root, namespace, name):
    return [
        (element.text or "").strip()
        for element in root.iter("{{{0}}}{1}".format(namespace, name))
        if (element.text or "").strip()
    ]


def archive_path(base, href):
    decoded = urllib.parse.unquote(href.split("#", 1)[0])
    return posixpath.normpath(posixpath.join(base, decoded))


def decode_css(data):
    for encoding in ("utf-8-sig", "utf-16", "gb18030", "big5"):
        try:
            return data.decode(encoding)
        except UnicodeDecodeError:
            continue
    return data.decode("utf-8", "replace")


def inspect(path):
    with zipfile.ZipFile(path) as archive:
        names = archive.namelist()
        name_set = set(names)
        container = ET.fromstring(archive.read("META-INF/container.xml"))
        rootfile = container.find(".//{{{0}}}rootfile".format(CONTAINER_NS))
        if rootfile is None:
            raise ValueError("container.xml has no rootfile")
        opf_path = rootfile.attrib.get("full-path", "")
        opf = ET.fromstring(archive.read(opf_path))
        opf_dir = posixpath.dirname(opf_path)
        manifest_element = opf.find("{{{0}}}manifest".format(OPF_NS))
        spine_element = opf.find("{{{0}}}spine".format(OPF_NS))
        if manifest_element is None or spine_element is None:
            raise ValueError("package has no manifest or spine")

        manifest = []
        by_id = {}
        for item in manifest_element:
            entry = dict(item.attrib)
            entry["archive_path"] = archive_path(opf_dir, entry.get("href", ""))
            manifest.append(entry)
            by_id[entry.get("id", "")] = entry

        spine = []
        for itemref in spine_element:
            entry = by_id.get(itemref.attrib.get("idref", ""), {})
            spine.append({
                "idref": itemref.attrib.get("idref", ""),
                "linear": itemref.attrib.get("linear", ""),
                "path": entry.get("archive_path", ""),
            })

        css_entries = [
            item for item in manifest
            if item.get("media-type") == "text/css" or item["archive_path"].lower().endswith(".css")
        ]
        css = "\n".join(
            decode_css(archive.read(item["archive_path"]))
            for item in css_entries if item["archive_path"] in name_set
        )
        css_without_comments = re.sub(r"/\*.*?\*/", "", css, flags=re.DOTALL)

        xhtml = [
            item for item in manifest
            if item.get("media-type") in {"application/xhtml+xml", "text/html"}
        ]
        images = [
            item for item in manifest
            if item.get("media-type", "").startswith("image/")
            or pathlib.PurePosixPath(item["archive_path"]).suffix.lower() in IMAGE_SUFFIXES
        ]
        fonts = [
            item for item in manifest
            if item.get("media-type", "").startswith(("font/", "application/font"))
            or pathlib.PurePosixPath(item["archive_path"]).suffix.lower() in {".otf", ".ttf", ".woff", ".woff2"}
        ]
        empty_text = [
            item["archive_path"] for item in manifest
            if item["archive_path"] in name_set
            and item.get("media-type") in {
                "application/xhtml+xml", "application/xml", "image/svg+xml", "text/css", "text/html"
            }
            and archive.getinfo(item["archive_path"]).file_size == 0
        ]

        return {
            "path": str(path),
            "epub_version": opf.attrib.get("version", ""),
            "package_path": opf_path,
            "unique_identifier": opf.attrib.get("unique-identifier", ""),
            "metadata": {
                "title": text_values(opf, DC_NS, "title"),
                "creator": text_values(opf, DC_NS, "creator"),
                "language": text_values(opf, DC_NS, "language"),
                "identifier": text_values(opf, DC_NS, "identifier"),
            },
            "counts": {
                "archive_entries": len(names),
                "manifest": len(manifest),
                "spine": len(spine),
                "xhtml": len(xhtml),
                "images": len(images),
                "stylesheets": len(css_entries),
                "fonts": len(fonts),
            },
            "navigation": {
                "nav_paths": [
                    item["archive_path"] for item in manifest
                    if "nav" in item.get("properties", "").split()
                ],
                "ncx_paths": [
                    item["archive_path"] for item in manifest
                    if item.get("media-type") == "application/x-dtbncx+xml"
                ],
            },
            "cover_paths": [
                item["archive_path"] for item in manifest
                if "cover-image" in item.get("properties", "").split()
            ],
            "spine_first": spine[:10],
            "spine_last": spine[-10:],
            "css_features": {
                "vertical_writing": bool(re.search(
                    r"writing-mode\s*:\s*(?:vertical|tb-)", css_without_comments, re.IGNORECASE
                )),
                "ruby_rules": bool(re.search(r"(?:\bruby\b|\brt\b)\s*[{,]", css_without_comments)),
                "page_break_rules": len(re.findall(
                    r"(?:page-break|break-before|break-after)\s*:", css_without_comments, re.IGNORECASE
                )),
                "font_faces": len(re.findall(r"@font-face\b", css_without_comments, re.IGNORECASE)),
                "scripts": sum(
                    "scripted" in item.get("properties", "").split() for item in manifest
                ),
            },
            "empty_text_resources": empty_text,
        }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("epub", nargs="+", type=pathlib.Path)
    parser.add_argument("--pretty", action="store_true")
    args = parser.parse_args()
    reports = []
    failed = False
    for path in args.epub:
        try:
            reports.append(inspect(path))
        except (OSError, ValueError, KeyError, zipfile.BadZipFile, ET.ParseError) as error:
            failed = True
            reports.append({"path": str(path), "error": str(error)})
    payload = reports[0] if len(reports) == 1 else reports
    json.dump(payload, sys.stdout, ensure_ascii=False, indent=2 if args.pretty else None)
    sys.stdout.write("\n")
    return 1 if failed else 0


if __name__ == "__main__":
    raise SystemExit(main())
