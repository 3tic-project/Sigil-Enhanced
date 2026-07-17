#!/usr/bin/env python3
"""Inventory local EPUB source files without emitting prose content."""

import argparse
import json
import pathlib
import re
import struct
import sys


TEXT_SUFFIXES = {".htm", ".html", ".md", ".txt", ".xhtml", ".xml"}
IMAGE_SUFFIXES = {".gif", ".jpeg", ".jpg", ".png", ".webp"}
RUBY_PATTERNS = [
    re.compile(r"\[ruby=[^]]+\].*?\[/ruby\]", re.IGNORECASE),
    re.compile(r"<ruby\b", re.IGNORECASE),
]
IMAGE_MARKER = re.compile(
    r"(?:［|\[)?(?:插图|插圖|illustration|image)\s*[:：]\s*"
    r"([^\]\[］［\r\n）)]+)",
    re.IGNORECASE,
)
FOOTNOTE_REFERENCE = re.compile(r"[（(]\s*[註注]\s*([0-9０-９]+)\s*[）)]")
FOOTNOTE_DEFINITION = re.compile(r"^[\t \u3000]*[註注]\s*([0-9０-９]+)\s*[:：]", re.MULTILINE)
FULLWIDTH_DIGITS = str.maketrans("０１２３４５６７８９", "0123456789")


def marker_ids(pattern, text):
    return [match.translate(FULLWIDTH_DIGITS) for match in pattern.findall(text)]


def decode_text(data):
    for encoding in ("utf-8-sig", "utf-16", "gb18030", "big5", "shift_jis"):
        try:
            return data.decode(encoding), encoding
        except UnicodeDecodeError:
            continue
    return data.decode("utf-8", "replace"), "utf-8-replacement"


def jpeg_dimensions(data):
    if not data.startswith(b"\xff\xd8"):
        return None
    position = 2
    sof = {0xC0, 0xC1, 0xC2, 0xC3, 0xC5, 0xC6, 0xC7, 0xC9, 0xCA, 0xCB, 0xCD, 0xCE, 0xCF}
    while position + 9 <= len(data):
        if data[position] != 0xFF:
            position += 1
            continue
        marker = data[position + 1]
        position += 2
        if marker in {0xD8, 0xD9}:
            continue
        if position + 2 > len(data):
            return None
        length = struct.unpack(">H", data[position:position + 2])[0]
        if marker in sof and position + 7 <= len(data):
            height, width = struct.unpack(">HH", data[position + 3:position + 7])
            return width, height
        if length < 2:
            return None
        position += length
    return None


def image_dimensions(data, suffix):
    if suffix == ".png" and data.startswith(b"\x89PNG\r\n\x1a\n") and len(data) >= 24:
        return struct.unpack(">II", data[16:24])
    if suffix == ".gif" and data[:6] in {b"GIF87a", b"GIF89a"} and len(data) >= 10:
        return struct.unpack("<HH", data[6:10])
    if suffix in {".jpg", ".jpeg"}:
        return jpeg_dimensions(data)
    if suffix == ".webp" and data.startswith(b"RIFF") and data[8:12] == b"WEBP":
        if data[12:16] == b"VP8X" and len(data) >= 30:
            width = 1 + int.from_bytes(data[24:27], "little")
            height = 1 + int.from_bytes(data[27:30], "little")
            return width, height
    return None


def inspect(root):
    files = sorted(path for path in root.rglob("*") if path.is_file())
    text_reports = []
    image_reports = []
    other = []
    for path in files:
        relative = path.relative_to(root).as_posix()
        suffix = path.suffix.lower()
        data = path.read_bytes()
        if suffix in TEXT_SUFFIXES:
            text, encoding = decode_text(data)
            lines = text.replace("\r\n", "\n").replace("\r", "\n").splitlines()
            ruby_count = sum(len(pattern.findall(text)) for pattern in RUBY_PATTERNS)
            markers = [match.group(1).strip() for match in IMAGE_MARKER.finditer(text)]
            footnote_references = marker_ids(FOOTNOTE_REFERENCE, text)
            footnote_definitions = marker_ids(FOOTNOTE_DEFINITION, text)
            reference_ids = set(footnote_references)
            definition_ids = set(footnote_definitions)
            text_reports.append({
                "path": relative,
                "bytes": len(data),
                "encoding": encoding,
                "lines": len(lines),
                "nonblank_lines": sum(bool(line.strip()) for line in lines),
                "ruby_markers": ruby_count,
                "image_markers": markers,
                "footnotes": {
                    "references": len(footnote_references),
                    "definitions": len(footnote_definitions),
                    "missing_definitions": sorted(reference_ids - definition_ids),
                    "unreferenced_definitions": sorted(definition_ids - reference_ids),
                },
                "scene_breaks": sum(
                    line.strip() in {"***", "＊ ＊ ＊", "＊＊＊", "---"} for line in lines
                ),
            })
        elif suffix in IMAGE_SUFFIXES:
            dimensions = image_dimensions(data, suffix)
            image_reports.append({
                "path": relative,
                "bytes": len(data),
                "width": dimensions[0] if dimensions else None,
                "height": dimensions[1] if dimensions else None,
                "orientation": (
                    "landscape" if dimensions and dimensions[0] > dimensions[1]
                    else "portrait" if dimensions and dimensions[1] > dimensions[0]
                    else "square" if dimensions else "unknown"
                ),
            })
        else:
            other.append({"path": relative, "bytes": len(data)})
    return {
        "source_directory": str(root),
        "counts": {"files": len(files), "text": len(text_reports), "images": len(image_reports), "other": len(other)},
        "text": text_reports,
        "images": image_reports,
        "other": other,
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=pathlib.Path)
    parser.add_argument("--pretty", action="store_true")
    args = parser.parse_args()
    if not args.source.is_dir():
        parser.error("source must be a directory")
    json.dump(inspect(args.source), sys.stdout, ensure_ascii=False, indent=2 if args.pretty else None)
    sys.stdout.write("\n")


if __name__ == "__main__":
    main()
