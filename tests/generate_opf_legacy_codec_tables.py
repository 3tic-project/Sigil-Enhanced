"""Regenerate the C++ GBK and Shift_JIS tables from the legacy CPython codecs.

The checked-in tables were generated with CPython 3.11.12, the current
embedded runtime. Run with that version before changing the frozen mapping.
This script is development tooling; the application only uses the header.
"""

import codecs
import sys
from pathlib import Path


OUTPUT = Path(__file__).resolve().parents[1] / "src" / "ResourceObjects" / "OPFLegacyCodecTables.h"
MISSING = 0xFFFF
CODECS = ("gbk", "shift_jis")


def mapping(encoding):
    single = [MISSING] * 256
    doubles = {}
    leads = []
    for byte in range(256):
        try:
            text = bytes([byte]).decode(encoding)
        except UnicodeDecodeError:
            continue
        if len(text) != 1 or ord(text) > 0xFFFF:
            raise SystemExit(f"{encoding} single byte {byte:#x} is not one BMP character")
        single[byte] = ord(text)
    for lead in range(256):
        if single[lead] != MISSING:
            continue
        row = [MISSING] * 256
        defined = False
        for trail in range(256):
            try:
                text = bytes([lead, trail]).decode(encoding)
            except UnicodeDecodeError:
                continue
            if len(text) != 1 or ord(text) > 0xFFFF:
                raise SystemExit(f"{encoding} pair {(lead, trail)} is not one BMP character")
            row[trail] = ord(text)
            defined = True
        if defined:
            doubles[lead] = row
            leads.append(lead)
    encoded = []
    for codepoint in range(0x10000):
        if 0xD800 <= codepoint <= 0xDFFF:
            continue
        try:
            raw = chr(codepoint).encode(encoding)
        except UnicodeEncodeError:
            continue
        if len(raw) == 1:
            unit = raw[0]
        elif len(raw) == 2:
            unit = (raw[0] << 8) | raw[1]
        else:
            raise SystemExit(f"{encoding} encodes U+{codepoint:04X} to {len(raw)} bytes")
        encoded.append((codepoint, unit))
    for codepoint in range(0x10000, 0x110000):
        try:
            chr(codepoint).encode(encoding)
        except UnicodeEncodeError:
            continue
        raise SystemExit(f"{encoding} encodes non-BMP U+{codepoint:04X}")
    return single, leads, doubles, encoded


class TableDecodeError(Exception):
    pass


def decode_with_tables(data, single, leads, doubles):
    lead_index = {lead: index for index, lead in enumerate(leads)}
    text = []
    index = 0
    while index < len(data):
        byte = data[index]
        if single[byte] != MISSING:
            text.append(chr(single[byte]))
            index += 1
            continue
        if byte not in lead_index or index + 1 >= len(data):
            raise TableDecodeError
        point = doubles[byte][data[index + 1]]
        if point == MISSING:
            raise TableDecodeError
        text.append(chr(point))
        index += 2
    return "".join(text)


def encode_with_tables(text, encoded):
    table = dict(encoded)
    raw = bytearray()
    for character in text:
        unit = table.get(ord(character))
        if unit is None:
            raise UnicodeEncodeError("legacy", text, 0, 1, "unmapped")
        if unit <= 0xFF:
            raw.append(unit)
        else:
            raw.append(unit >> 8)
            raw.append(unit & 0xFF)
    return bytes(raw)


def verify(encoding, single, leads, doubles, encoded):
    for size in (1, 2):
        limit = 256 if size == 1 else 256 * 256
        for value in range(limit):
            data = bytes([value]) if size == 1 else bytes((value >> 8, value & 0xFF))
            try:
                expected = data.decode(encoding)
            except UnicodeDecodeError:
                expected = None
            try:
                actual = decode_with_tables(data, single, leads, doubles)
            except TableDecodeError:
                actual = None
            if actual != expected:
                raise SystemExit(f"{encoding} decode mismatch for {data.hex()}: {expected!r} != {actual!r}")
    for codepoint, _unit in encoded:
        character = chr(codepoint)
        if encode_with_tables(character, encoded) != character.encode(encoding):
            raise SystemExit(f"{encoding} encode mismatch for U+{codepoint:04X}")
    sample = "ASCII 中文 日本 café"
    try:
        expected = sample.encode(encoding)
    except UnicodeEncodeError:
        expected = None
    try:
        actual = encode_with_tables(sample, encoded)
    except UnicodeEncodeError:
        actual = None
    if actual != expected:
        raise SystemExit(f"{encoding} sample encode mismatch")


def emit_array(lines, name, values, width):
    lines.append(f"inline constexpr std::uint16_t {name}[] = {{")
    for offset in range(0, len(values), width):
        row = ", ".join(f"0x{value:04x}" for value in values[offset:offset + width])
        lines.append(f"    {row},")
    lines.append("};")
    lines.append("")


def emit(encoding, single, leads, doubles, encoded):
    stem = "Gbk" if encoding == "gbk" else "ShiftJis"
    lines = []
    emit_array(lines, f"k{stem}Single", single, 16)
    lead_index = [0xFF] * 256
    pairs = []
    for index, lead in enumerate(leads):
        lead_index[lead] = index
        pairs.extend(doubles[lead])
    lines.append(f"inline constexpr std::uint8_t k{stem}Lead[] = {{")
    for offset in range(0, 256, 16):
        row = ", ".join(f"0x{value:02x}" for value in lead_index[offset:offset + 16])
        lines.append(f"    {row},")
    lines.append("};")
    lines.append("")
    emit_array(lines, f"k{stem}Pairs", pairs, 16)
    emit_array(lines, f"k{stem}EncodeCp", [codepoint for codepoint, _unit in encoded], 16)
    emit_array(lines, f"k{stem}EncodeUnit", [unit for _codepoint, unit in encoded], 16)
    lines.append(f"inline constexpr Codec k{stem} = {{")
    lines.append(f"    k{stem}Single, k{stem}Lead, k{stem}Pairs,")
    lines.append(f"    k{stem}EncodeCp, k{stem}EncodeUnit, {len(encoded)}")
    lines.append("};")
    lines.append("")
    return lines


def main():
    if sys.version_info[:3] != (3, 11, 12):
        raise SystemExit(f"Generate these tables with CPython 3.11.12, not {sys.version.split()[0]}")
    sections = []
    for encoding in CODECS:
        single, leads, doubles, encoded = mapping(encoding)
        verify(encoding, single, leads, doubles, encoded)
        sections.extend(emit(encoding, single, leads, doubles, encoded))
        print(f"{encoding}: {sum(value != MISSING for value in single)} single-byte, "
              f"{len(leads)} lead bytes, {len(encoded)} encodable characters")
    # Touch codecs so a missing stdlib codec fails before the header is replaced.
    for encoding in CODECS:
        codecs.lookup(encoding)
    header = [
        "// Generated by tests/generate_opf_legacy_codec_tables.py from CPython 3.11.12.",
        "// Do not edit by hand. These values freeze the legacy OPF GBK and Shift_JIS codecs.",
        "#pragma once",
        "",
        "#include <cstdint>",
        "",
        "namespace OPFLegacyCodecTables {",
        "",
        "inline constexpr std::uint16_t kMissing = 0xffff;",
        "",
        "struct Codec {",
        "    const std::uint16_t *single;",
        "    const std::uint8_t *lead;",
        "    const std::uint16_t *pairs;",
        "    const std::uint16_t *encode_cp;",
        "    const std::uint16_t *encode_unit;",
        "    int encode_count;",
        "};",
        "",
        *sections,
        "}",
        "",
    ]
    OUTPUT.write_text("\n".join(header), encoding="utf-8")
    print(f"wrote {OUTPUT}")


if __name__ == "__main__":
    main()
