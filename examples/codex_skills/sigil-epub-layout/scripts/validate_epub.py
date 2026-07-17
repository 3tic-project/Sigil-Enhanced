#!/usr/bin/env python3
"""Validate deterministic EPUB container, package, XML, and local-reference invariants."""

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
XHTML_NS = "http://www.w3.org/1999/xhtml"
TEXT_MEDIA_TYPES = {
    "application/xhtml+xml", "application/xml", "application/x-dtbncx+xml",
    "image/svg+xml", "text/css", "text/html",
}
XML_MEDIA_TYPES = {
    "application/xhtml+xml", "application/xml", "application/x-dtbncx+xml",
    "image/svg+xml", "text/html",
}
EXTERNAL_SCHEMES = {"data", "ftp", "http", "https", "mailto", "tel"}
CSS_URL = re.compile(r"url\(\s*(['\"]?)(.*?)\1\s*\)", re.IGNORECASE)


def local_name(tag):
    return tag.rsplit("}", 1)[-1]


def canonical_path(base, target):
    target = urllib.parse.unquote(target.split("#", 1)[0].split("?", 1)[0])
    return posixpath.normpath(posixpath.join(base, target))


def is_external(target):
    return urllib.parse.urlsplit(target).scheme.lower() in EXTERNAL_SCHEMES


def decode_text(data):
    for encoding in ("utf-8-sig", "utf-16", "gb18030", "big5"):
        try:
            return data.decode(encoding)
        except UnicodeDecodeError:
            continue
    return data.decode("utf-8", "replace")


class Report:
    def __init__(self, path):
        self.path = str(path)
        self.errors = []
        self.warnings = []
        self.counts = {}

    def error(self, message):
        if message not in self.errors:
            self.errors.append(message)

    def warn(self, message):
        if message not in self.warnings:
            self.warnings.append(message)

    def payload(self):
        return {
            "path": self.path,
            "valid": not self.errors,
            "errors": self.errors,
            "warnings": self.warnings,
            "counts": self.counts,
        }


def validate(path, strict_layout=False):
    report = Report(path)
    try:
        archive = zipfile.ZipFile(path)
    except (OSError, zipfile.BadZipFile) as error:
        report.error("cannot open EPUB ZIP: {0}".format(error))
        return report

    with archive:
        names = archive.namelist()
        name_set = set(names)
        report.counts["archive_entries"] = len(names)
        if len(names) != len(name_set):
            report.error("archive contains duplicate entry names")
        for name in names:
            normalized = posixpath.normpath(name)
            if name.startswith("/") or normalized == ".." or normalized.startswith("../"):
                report.error("archive entry escapes the EPUB root: " + name)
        bad_member = archive.testzip()
        if bad_member:
            report.error("ZIP CRC failed: " + bad_member)
        if not names or names[0] != "mimetype":
            report.error("mimetype must be the first ZIP entry")
        elif archive.getinfo("mimetype").compress_type != zipfile.ZIP_STORED:
            report.error("mimetype must be stored without compression")
        elif archive.read("mimetype") != b"application/epub+zip":
            report.error("mimetype content is not application/epub+zip")

        try:
            container_data = archive.read("META-INF/container.xml")
            container = ET.fromstring(container_data)
            rootfile = container.find(".//{{{0}}}rootfile".format(CONTAINER_NS))
            if rootfile is None or not rootfile.attrib.get("full-path"):
                raise ValueError("container has no rootfile path")
            opf_path = rootfile.attrib["full-path"]
            opf_data = archive.read(opf_path)
            opf = ET.fromstring(opf_data)
        except (KeyError, ValueError, ET.ParseError) as error:
            report.error("container/package error: {0}".format(error))
            return report

        version = opf.attrib.get("version", "")
        report.counts["epub_version"] = version
        opf_dir = posixpath.dirname(opf_path)
        metadata = opf.find("{{{0}}}metadata".format(OPF_NS))
        manifest_element = opf.find("{{{0}}}manifest".format(OPF_NS))
        spine_element = opf.find("{{{0}}}spine".format(OPF_NS))
        if metadata is None or manifest_element is None or spine_element is None:
            report.error("package requires metadata, manifest, and spine")
            return report

        ids = set()
        paths = set()
        manifest = []
        by_id = {}
        for item in manifest_element:
            item_id = item.attrib.get("id", "")
            href = item.attrib.get("href", "")
            media_type = item.attrib.get("media-type", "")
            if not item_id or not href or not media_type:
                report.error("manifest items require id, href, and media-type")
                continue
            if item_id in ids:
                report.error("duplicate manifest ID: " + item_id)
            ids.add(item_id)
            archive_item_path = canonical_path(opf_dir, href)
            if archive_item_path.startswith("../") or archive_item_path.startswith("/"):
                report.error("manifest path escapes the EPUB root: " + href)
            if archive_item_path in paths:
                report.error("duplicate manifest path: " + archive_item_path)
            paths.add(archive_item_path)
            if archive_item_path not in name_set:
                report.error("manifest target is missing: " + archive_item_path)
            entry = {
                "id": item_id,
                "path": archive_item_path,
                "media_type": media_type,
                "properties": item.attrib.get("properties", "").split(),
            }
            manifest.append(entry)
            by_id[item_id] = entry

        spine = []
        for itemref in spine_element:
            idref = itemref.attrib.get("idref", "")
            if idref not in by_id:
                report.error("spine idref is missing from manifest: " + idref)
                continue
            spine.append(by_id[idref]["path"])
            if by_id[idref]["media_type"] not in {"application/xhtml+xml", "text/html"}:
                report.error("spine item is not HTML/XHTML: " + by_id[idref]["path"])

        report.counts.update({
            "manifest": len(manifest),
            "spine": len(spine),
            "xhtml": sum(item["media_type"] == "application/xhtml+xml" for item in manifest),
            "images": sum(item["media_type"].startswith("image/") for item in manifest),
            "stylesheets": sum(item["media_type"] == "text/css" for item in manifest),
        })

        local_references = 0
        ruby_count = 0
        footnote_count = 0
        for item in manifest:
            resource_path = item["path"]
            if resource_path not in name_set:
                continue
            data = archive.read(resource_path)
            if item["media_type"] in TEXT_MEDIA_TYPES and not data:
                report.error("text resource is empty: " + resource_path)
                continue
            if item["media_type"] in XML_MEDIA_TYPES:
                try:
                    root = ET.fromstring(data)
                except ET.ParseError as error:
                    report.error("XML parse failed for {0}: {1}".format(resource_path, error))
                    continue
                ruby_count += sum(local_name(element.tag) == "ruby" for element in root.iter())
                footnote_count += sum(
                    "footnote" in element.attrib.get(
                        "{http://www.idpf.org/2007/ops}type", ""
                    ).split()
                    for element in root.iter()
                )
                for element in root.iter():
                    for attribute, target in element.attrib.items():
                        if local_name(attribute) not in {"href", "src", "poster"}:
                            continue
                        if not target or target.startswith("#") or is_external(target):
                            continue
                        resolved = canonical_path(posixpath.dirname(resource_path), target)
                        local_references += 1
                        if resolved not in name_set:
                            report.error(
                                "unresolved reference in {0}: {1}".format(resource_path, target)
                            )
            elif item["media_type"] == "text/css":
                css = decode_text(data)
                for match in CSS_URL.finditer(css):
                    target = match.group(2).strip()
                    if not target or target.startswith("#") or is_external(target):
                        continue
                    resolved = canonical_path(posixpath.dirname(resource_path), target)
                    local_references += 1
                    if resolved not in name_set:
                        report.error(
                            "unresolved CSS URL in {0}: {1}".format(resource_path, target)
                        )

        report.counts["local_references"] = local_references
        report.counts["ruby"] = ruby_count
        report.counts["footnotes"] = footnote_count

        titles = [
            (element.text or "").strip()
            for element in metadata.iter("{{{0}}}title".format(DC_NS))
            if (element.text or "").strip()
        ]
        languages = [
            (element.text or "").strip()
            for element in metadata.iter("{{{0}}}language".format(DC_NS))
            if (element.text or "").strip()
        ]
        creators = [
            (element.text or "").strip()
            for element in metadata.iter("{{{0}}}creator".format(DC_NS))
            if (element.text or "").strip()
        ]
        identifiers = {
            element.attrib.get("id", ""): (element.text or "").strip()
            for element in metadata.iter("{{{0}}}identifier".format(DC_NS))
        }
        unique_identifier = opf.attrib.get("unique-identifier", "")
        modified = [
            (element.text or "").strip()
            for element in metadata.iter("{{{0}}}meta".format(OPF_NS))
            if element.attrib.get("property") == "dcterms:modified"
            and (element.text or "").strip()
        ]
        nav_items = [item for item in manifest if "nav" in item["properties"]]
        cover_items = [item for item in manifest if "cover-image" in item["properties"]]

        if not titles:
            report.error("dc:title is missing")
        if not languages:
            report.error("dc:language is missing")
        if not unique_identifier or not identifiers.get(unique_identifier):
            report.error("package unique-identifier does not resolve to a nonempty dc:identifier")
        if version.startswith("3") and len(nav_items) != 1:
            report.error("EPUB 3 requires exactly one nav manifest item")

        if strict_layout:
            if not creators:
                report.error("strict layout requires dc:creator")
            if version.startswith("3") and not modified:
                report.error("strict EPUB 3 layout requires dcterms:modified")
            if not cover_items:
                report.error("strict layout requires a cover-image manifest item")
            if not spine:
                report.error("strict layout requires a nonempty spine")
            if not report.counts["stylesheets"]:
                report.error("strict layout requires at least one stylesheet")
            if version.startswith("3") and nav_items:
                nav_path = nav_items[0]["path"]
                if nav_path in name_set:
                    try:
                        nav_root = ET.fromstring(archive.read(nav_path))
                        toc = [
                            element for element in nav_root.iter("{{{0}}}nav".format(XHTML_NS))
                            if "toc" in element.attrib.get(
                                "{http://www.idpf.org/2007/ops}type", ""
                            ).split()
                        ]
                        if len(toc) != 1:
                            report.error("nav document requires exactly one EPUB toc nav")
                        elif "doc-toc" not in toc[0].attrib.get("role", "").split():
                            report.error("EPUB toc nav requires role=doc-toc")
                    except ET.ParseError:
                        pass
        elif not cover_items:
            report.warn("no EPUB 3 cover-image property was found")

    return report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("epub", type=pathlib.Path)
    parser.add_argument("--strict-layout", action="store_true")
    parser.add_argument("--json", action="store_true")
    args = parser.parse_args()
    report = validate(args.epub, strict_layout=args.strict_layout)
    payload = report.payload()
    if args.json:
        json.dump(payload, sys.stdout, ensure_ascii=False, indent=2)
        sys.stdout.write("\n")
    else:
        print("VALID" if payload["valid"] else "INVALID", payload["path"])
        for message in payload["errors"]:
            print("ERROR:", message)
        for message in payload["warnings"]:
            print("WARNING:", message)
        print("COUNTS:", json.dumps(payload["counts"], ensure_ascii=False, sort_keys=True))
    return 0 if payload["valid"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
