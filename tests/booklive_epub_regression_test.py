"""Verify private BookLive EPUBs without adding their text to the repository."""

import argparse
import copy
import hashlib
import json
import os
import pathlib
import subprocess
import sys
import xml.etree.ElementTree as ET
import zipfile


def local_name(element):
    return element.tag.rsplit("}", 1)[-1]


def text_nodes(node, in_paragraph=False):
    if local_name(node) in ("script", "style"):
        return
    in_paragraph |= local_name(node) in ("p", "h1", "h2", "h3", "h4", "h5", "h6")
    if node.text and node.text.strip():
        yield node.text, in_paragraph
    for child in node:
        yield from text_nodes(child, in_paragraph)
        if child.tail and child.tail.strip():
            yield child.tail, in_paragraph


def attributes(root):
    return [(name, node.get(name)) for node in root.iter()
            for name in ("id", "name", "href", "src") if name in node.attrib]


def mixed_flow(root):
    for node in root.iter():
        if local_name(node) != "div" or not any(local_name(c) == "div" for c in node):
            continue
        if any(text and text.strip() for text in [node.text] + [c.tail for c in node]):
            return True
    return False


def check_book(probe, path, output_dir):
    original_hash = hashlib.sha256(path.read_bytes()).hexdigest()
    with zipfile.ZipFile(path) as archive:
        requests = [{"path": name, "text": archive.read(name).decode("utf-8")}
                    for name in archive.namelist() if name.endswith(".xhtml")]
        process = subprocess.run([probe], input=json.dumps(requests), text=True,
                                 capture_output=True, check=True, timeout=60)
        results = json.loads(process.stdout)
        assert len(results) == len(requests)
        changed = {}
        mixed_pages = []
        total_conversions = 0
        for source, result in zip(requests, results):
            name = source["path"]
            assert result["path"] == name
            context = f"{path.name}: {name}: {result['messages']}"
            before = ET.fromstring(source["text"])
            after = ET.fromstring(result["text"])
            assert result["idempotent"], "Not idempotent: " + context
            if result["safe"]:
                assert result["ok"] and result["changed"], "Safe page failed: " + context
            if mixed_flow(before):
                mixed_pages.append(name)
                assert result["safe"] and result["changed"], "Mixed flow skipped: " + context
                body = next(node for node in after.iter() if local_name(node) == "body")
                outside = "".join(text for text, inside in text_nodes(body) if not inside)
                assert not outside, "Text remains outside translatable paragraphs: " + context
            assert "".join(t for t, _ in text_nodes(before)) == "".join(
                t for t, _ in text_nodes(after)), "Text changed: " + context
            assert attributes(before) == attributes(after), "IDs/links changed: " + context
            for tag in ("ruby", "rt", "img"):
                assert sum(local_name(n) == tag for n in before.iter()) == sum(
                    local_name(n) == tag for n in after.iter()), f"{tag} count changed: " + context
            for space in ("\u3000", "\u00a0"):
                assert "".join(before.itertext()).count(space) == "".join(after.itertext()).count(space), (
                    "Layout spacing changed: " + context)
            if result["changed"]:
                changed[name] = result["text"].encode("utf-8")
                total_conversions += result["conversions"]
            if name.endswith("/p-007.xhtml"):
                print(f"  {name}: {result['kind']}, {result['conversions']} conversions")
        if output_dir:
            output_dir.mkdir(parents=True, exist_ok=True)
            destination = output_dir / path.name
            assert destination.resolve() != path.resolve(), "Output must not overwrite the source EPUB"
            with zipfile.ZipFile(destination, "w") as output:
                output.comment = archive.comment
                for info in archive.infolist():
                    output.writestr(copy.copy(info), changed.get(info.filename, archive.read(info.filename)))
            with zipfile.ZipFile(destination) as output:
                assert output.testzip() is None
                for info in archive.infolist():
                    expected = changed.get(info.filename, archive.read(info.filename))
                    assert output.read(info.filename) == expected
    assert hashlib.sha256(path.read_bytes()).hexdigest() == original_hash, "Source EPUB was modified"
    print(f"{path.name}: {len(requests)} XHTML, {len(changed)} updated, "
          f"{len(mixed_pages)} split flows repaired, {total_conversions} conversions")
    return len(mixed_pages)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("probe", type=pathlib.Path)
    default_dir = pathlib.Path(__file__).parents[1] / "todo" / "booklive_fix" / "epubs"
    parser.add_argument("--epub-dir", type=pathlib.Path,
                        default=pathlib.Path(os.environ.get("SIGIL_BOOKLIVE_TEST_EPUB_DIR", str(default_dir))))
    parser.add_argument("--output-dir", type=pathlib.Path)
    args = parser.parse_args()
    books = sorted(args.epub_dir.glob("*.epub"))
    if not books:
        print("SKIP: set SIGIL_BOOKLIVE_TEST_EPUB_DIR to the private BookLive fixtures", file=sys.stderr)
        raise SystemExit(77)
    repaired = sum(check_book(args.probe.resolve(), book, args.output_dir) for book in books)
    assert repaired, "No split-flow regression pages were tested"
    print(f"BookLive private EPUB regression passed: {len(books)} books, {repaired} split-flow pages")
