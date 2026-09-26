"""Regenerate the frozen Unicode 14 name archive used by CodepointNames.

Use CPython 3.11 with its Unicode 14 database. This is development tooling;
the editor reads only the generated resource through C++.
"""

import struct
import unicodedata
import zlib
from pathlib import Path


OUTPUT = (Path(__file__).resolve().parents[1] / "src" / "Resource_Files"
          / "data" / "codepoint_names_unicode14.bin")


def main():
    if unicodedata.unidata_version != "14.0.0":
        raise SystemExit(
            f"Unicode 14.0.0 is required; got {unicodedata.unidata_version}"
        )

    entries = []
    names = bytearray()
    for codepoint in range(32, 0x110000):
        name = unicodedata.name(chr(codepoint), None)
        if name is None:
            continue
        encoded = name.encode("ascii")
        entries.append((codepoint, len(names)))
        names.extend(encoded)
        names.append(0)

    raw = (b"CPN1" + struct.pack("<II", len(entries), len(names))
           + b"".join(struct.pack("<II", *entry) for entry in entries) + names)
    # qUncompress expects the uncompressed size in four big-endian bytes.
    archive = struct.pack(">I", len(raw)) + zlib.compress(raw, level=9)
    OUTPUT.parent.mkdir(parents=True, exist_ok=True)
    OUTPUT.write_bytes(archive)
    print(f"wrote {OUTPUT}: {len(entries)} names, {len(archive)} bytes")


if __name__ == "__main__":
    main()
