"""Freeze CPython 3.11.12 codec lookup and the stateless OPF codecs for C++.

Writes src/Resource_Files/data/opf_legacy_codecs.bin, read by OPFSourceBytes.
It contains encodings.aliases, the importable encodings modules with their
codec names, and for each frozen codec a byte trie for decoding plus the
code point to byte table used for encoding. Every table is checked against
the CPython codec before the file is written. This is development tooling;
the application only reads the generated resource.
"""

import codecs
import importlib
import pkgutil
import random
import struct
import sys
import zlib
from pathlib import Path

import encodings
from encodings.aliases import aliases


OUTPUT = Path(__file__).resolve().parents[1] / "src" / "Resource_Files" / "data" / "opf_legacy_codecs.bin"
MISSING = 0xFFFFFFFF
CHILD = 0x80000000
# Stateless multibyte codecs. euc_kr (KS X 1001 make-up sequences), big5hkscs and
# the JIS X 0213 family (combining pairs), the ISO-2022 family, hz and utf-7
# (shift states) cannot be expressed as a byte trie and stay in Python.
MULTIBYTE = ("big5", "cp932", "cp949", "cp950", "euc_jp", "gb2312", "johab")
# Covered by the existing GBK/Shift_JIS header, Qt or hand-written C++.
SKIP_CHARMAP = {"cp1252", "iso8859-1"}


def modules():
    result = {}
    for info in pkgutil.iter_modules(encodings.__path__):
        try:
            module = importlib.import_module("encodings." + info.name)
        except ImportError:
            continue
        getregentry = getattr(module, "getregentry", None)
        result[info.name] = (getregentry().name if getregentry else "", module)
    return result


def trie(codec):
    """Decode trie built from CPython's incremental decoder."""
    nodes = []

    def build(prefix):
        index = len(nodes)
        nodes.append([MISSING] * 256)
        for byte in range(256):
            data = prefix + bytes([byte])
            decoder = codecs.getincrementaldecoder(codec)()
            try:
                text = decoder.decode(data, final=False)
            except UnicodeDecodeError:
                continue
            if text == "":
                if len(data) >= 3:
                    raise SystemExit(f"{codec}: sequences longer than 3 bytes at {data.hex()}")
                nodes[index][byte] = CHILD | build(data)
            elif len(text) == 1:
                if decoder.decode(b"", final=True) != "":
                    raise SystemExit(f"{codec}: pending output after {data.hex()}")
                nodes[index][byte] = ord(text)
            else:
                raise SystemExit(f"{codec}: {data.hex()} decodes to {text!r}")
        return index

    build(b"")
    return nodes


def encode_table(codec, candidates):
    table = {}
    for codepoint in sorted(candidates):
        try:
            raw = chr(codepoint).encode(codec)
        except UnicodeEncodeError:
            continue
        if not 1 <= len(raw) <= 3:
            raise SystemExit(f"{codec}: U+{codepoint:04X} encodes to {raw.hex()}")
        table[codepoint] = raw
    rest = "".join(chr(cp) for cp in range(0x110000) if cp not in candidates and not 0xD800 <= cp <= 0xDFFF)
    if rest.encode(codec, "ignore"):
        raise SystemExit(f"{codec}: encodes characters outside the candidate set")
    return table


def decode_with(nodes, data):
    text = []
    index = 0
    while index < len(data):
        node = 0
        while True:
            entry = nodes[node][data[index]]
            index += 1
            if entry == MISSING:
                return None
            if entry & CHILD:
                if index >= len(data):
                    return None
                node = entry & ~CHILD
                continue
            text.append(chr(entry))
            break
    return "".join(text)


def verify(codec, nodes, table):
    def expected(data):
        try:
            return data.decode(codec)
        except UnicodeDecodeError:
            return None

    for first in range(256):
        data = bytes([first])
        if decode_with(nodes, data) != expected(data):
            raise SystemExit(f"{codec}: decode differs at {data.hex()}")
        if nodes[0][first] & CHILD and nodes[0][first] != MISSING:
            for second in range(256):
                data = bytes([first, second])
                if decode_with(nodes, data) != expected(data):
                    raise SystemExit(f"{codec}: decode differs at {data.hex()}")
    generator = random.Random(codec)
    units = [raw for raw in table.values()]
    for _ in range(20000):
        data = b"".join(generator.choice(units) if generator.random() < 0.9 else bytes([generator.randrange(256)])
                        for _ in range(generator.randrange(1, 12)))
        if decode_with(nodes, data) != expected(data):
            raise SystemExit(f"{codec}: decode differs at {data.hex()}")
    characters = list(table)
    for _ in range(20000):
        text = "".join(chr(generator.choice(characters)) for _ in range(generator.randrange(1, 12)))
        if text.encode(codec) != b"".join(table[ord(c)] for c in text):
            raise SystemExit(f"{codec}: encode differs for {text!r}")


def main():
    if sys.version_info[:3] != (3, 11, 12):
        raise SystemExit(f"Generate this table with CPython 3.11.12, not {sys.version.split()[0]}")
    found = modules()
    frozen = []
    for name, (codec, module) in sorted(found.items()):
        charmap = hasattr(module, "decoding_table") or hasattr(module, "decoding_map")
        if (charmap and codec not in SKIP_CHARMAP) or name in MULTIBYTE:
            frozen.append((codec, module, name in MULTIBYTE))
    raw = bytearray(b"OLC1")

    def text(value):
        encoded = value.encode("ascii")
        raw.extend(struct.pack("<B", len(encoded)) + encoded)

    raw.extend(struct.pack("<I", len(aliases)))
    for alias, target in sorted(aliases.items()):
        text(alias)
        text(target)
    raw.extend(struct.pack("<I", len(found)))
    for name, (codec, _module) in sorted(found.items()):
        text(name)
        text(codec)
    raw.extend(struct.pack("<I", len(frozen)))
    for codec, module, multibyte in frozen:
        nodes = trie(codec)
        candidates = {entry for node in nodes for entry in node if entry != MISSING and not entry & CHILD}
        encoding_map = getattr(module, "encoding_map", None) or {}
        candidates |= {key for key in encoding_map if isinstance(key, int)}
        if multibyte:
            candidates |= {cp for cp in range(0x10000) if not 0xD800 <= cp <= 0xDFFF}
        table = encode_table(codec, candidates)
        verify(codec, nodes, table)
        text(codec)
        raw.extend(struct.pack("<I", len(nodes)))
        for node in nodes:
            raw.extend(struct.pack("<256I", *node))
        raw.extend(struct.pack("<I", len(table)))
        for codepoint, encoded in sorted(table.items()):
            raw.extend(struct.pack("<I", codepoint) + struct.pack("<B", len(encoded)) + encoded.ljust(3, b"\0"))
        print(f"{codec}: {len(nodes)} trie nodes, {len(table)} encodable characters")
    archive = struct.pack(">I", len(raw)) + zlib.compress(bytes(raw), level=9)
    OUTPUT.write_bytes(archive)
    print(f"wrote {OUTPUT}: {len(frozen)} codecs, {len(archive)} bytes")


if __name__ == "__main__":
    main()
