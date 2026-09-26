"""Compare OPF byte codecs to the legacy Python implementation."""

import codecs
import random
import subprocess
import sys
from encodings.aliases import aliases
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "src/Resource_Files/python3lib"))
from opf_source_bytes import _DECLARATION, _encoding, decode_source, encode_source


NATIVE_ENCODINGS = {
    "utf-8", "utf-8-sig", "utf-16", "utf-16-le", "utf-16-be",
    "utf-32", "utf-32-le", "utf-32-be", "iso8859-1", "ascii", "cp1252",
    "gbk", "shift_jis", "gb18030",
}
MULTIBYTE = ("big5", "cp932", "cp949", "cp950", "euc_jp", "gb2312", "johab")


def frozen_codecs():
    import encodings
    import importlib
    import pkgutil
    names = set(MULTIBYTE)
    for info in pkgutil.iter_modules(encodings.__path__):
        try:
            module = importlib.import_module("encodings." + info.name)
        except ImportError:
            continue
        if hasattr(module, "decoding_table") or hasattr(module, "decoding_map"):
            names.add(module.getregentry().name)
    return sorted(names - {"cp1252", "iso8859-1"})


FROZEN = frozen_codecs()
NATIVE_ENCODINGS |= set(FROZEN)


def resolvable(name):
    # The C++ lookup leaves non-ASCII names to the Python fallback.
    if not name.isascii():
        return None
    try:
        return codecs.lookup(name).name
    except LookupError:
        return None


def is_native(operation, original, source):
    header = _DECLARATION.match(original[:1024].decode("latin-1"))
    if header and resolvable(header[2]) is None:
        return False
    try:
        old_encoding, bom = _encoding(original)
    except LookupError:
        return False
    if old_encoding not in NATIVE_ENCODINGS:
        return False
    if bom:
        try:
            declaration = _DECLARATION.match(original[len(bom):].decode(old_encoding))
        except UnicodeError:
            return False
        if declaration and resolvable(declaration[2]) is None:
            return False
    if operation == "E":
        declaration = _DECLARATION.match(source)
        if declaration and resolvable(declaration[2]) not in NATIVE_ENCODINGS:
            return False
    return True


def cases():
    base = "<?xml version='1.0' encoding='{name}'?><package>café 日本 𠮷</package>"
    for name, codec, bom in (
        ("UTF-8", "utf-8", b""),
        ("UTF-8", "utf-8", codecs.BOM_UTF8),
        ("UTF-16", "utf-16-le", codecs.BOM_UTF16_LE),
        ("UTF-16", "utf-16-be", codecs.BOM_UTF16_BE),
        ("UTF-32", "utf-32-le", codecs.BOM_UTF32_LE),
        ("UTF-32", "utf-32-be", codecs.BOM_UTF32_BE),
        ("utf-16-le", "utf-16-le", b""),
        ("utf-16-be", "utf-16-be", b""),
        ("utf-32-le", "utf-32-le", b""),
        ("utf-32-be", "utf-32-be", b""),
    ):
        source = base.format(name=name)
        original = bom + source.encode(codec)
        yield "D", original, ""
        yield "E", original, source.replace("日本", "Chinese 中文")
        yield "E", original, source.replace(name, "UTF-8", 1)
        yield "E", original, source.replace(" encoding='" + name + "'", "", 1)

    for name, codec, text in (
        ("ISO-8859-1", "iso-8859-1", "café"),
        ("latin-1", "latin-1", "café"),
        ("Windows-1252", "cp1252", "€ café"),
        ("US-ASCII", "ascii", "plain"),
        ("Shift_JIS", "shift_jis", "日本"),
        ("GBK", "gbk", "中文"),
    ):
        source = f"<?xml version='1.0' encoding='{name}'?><package>{text}</package>"
        original = source.encode(codec)
        yield "D", original, ""
        yield "E", original, source.replace(text, text + " added")
        yield "E", original, source.replace(text, "日本 𠮷")
        yield "E", original, source.replace(name, "UTF-8", 1).replace(text, "日本 𠮷")

    for data in (b"", b"\xff", b"\xef\xbb\xbf\xff", b"\xff\xfe\x00", b"\x00\x00\xfe\xff\x00\x00", b"<?xml encoding='no-such-codec'?><package/>", codecs.BOM_UTF8 + b"<?xml encoding='UTF-16'?><package/>"):
        yield "D", data, ""
    for data in (codecs.BOM_UTF8 * 2 + b"<package/>",
                 codecs.BOM_UTF16_LE * 2 + "<package/>".encode("utf-16-le"),
                 codecs.BOM_UTF32_BE * 2 + "<package/>".encode("utf-32-be")):
        yield "D", data, ""

    for data in (b"\xff\xfe\x00\xd8", b"\xff\xfe\x00\x00\x00\xd8\x00\x00",
                 b"\xff\xfe\x00\x00\x00\x00\x11\x00",
                 b"<?xml encoding='US-ASCII'?>\x80", b"<?xml encoding='Windows-1252'?>\x81"):
        yield "D", data, ""
    utf8 = b"<?xml encoding='UTF-8'?><package/>"
    for new_name in ("UTF-16", "UTF-32", "UTF-8-SIG", "ISO-8859-1"):
        yield "E", utf8, f"<?xml encoding='{new_name}'?><package/>"

    for name in ("ISO-8859-1", "Windows-1252", "US-ASCII"):
        prefix = f"<?xml encoding='{name}'?>".encode()
        for byte in range(256):
            yield "D", prefix + bytes([byte]), ""
            try:
                character = bytes([byte]).decode(name)
            except UnicodeDecodeError:
                continue
            yield "E", prefix, prefix.decode() + character

    for name, codec, text in (
        ("cp936", "gbk", "中文"),
        ("ms936", "gbk", "中文"),
        ("csshiftjis", "shift_jis", "日本"),
        ("shiftjis", "shift_jis", "日本"),
    ):
        source = f"<?xml encoding='{name}'?><package>{text}</package>"
        original = source.encode(codec)
        yield "D", original, ""
        yield "E", original, source + " "
    # Qt calls MS_Kanji Shift_JIS and windows-936 GBK. CPython does not.
    yield "D", "<?xml encoding='ms_kanji'?><package>日本</package>".encode("cp932"), ""
    yield "D", b"<?xml encoding='windows-936'?><package/>", ""

    for name, codec in (("GBK", "gbk"), ("Shift_JIS", "shift_jis")):
        prefix = f"<?xml encoding='{name}'?>".encode(codec)
        text = f"<?xml encoding='{name}'?>"
        for byte in range(256):
            yield "D", prefix + bytes([byte]), ""
        for first in range(256):
            for second in range(256):
                yield "D", prefix + bytes((first, second)), ""
        for codepoint in range(0x10000):
            if 0xD800 <= codepoint <= 0xDFFF:
                continue
            character = chr(codepoint)
            try:
                character.encode(codec)
            except UnicodeEncodeError:
                continue
            yield "E", prefix, text + character
        yield "E", prefix, text + "𠮷"

    for codec in FROZEN:
        prefix = f"<?xml encoding='{codec}'?>".encode("ascii")
        text = prefix.decode()
        leads = []
        for byte in range(256):
            yield "D", prefix + bytes([byte]), ""
            try:
                bytes([byte]).decode(codec)
            except UnicodeDecodeError:
                leads.append(byte)
        if codec in MULTIBYTE:
            for first in leads:
                for second in range(256):
                    yield "D", prefix + bytes((first, second)), ""
            if codec == "euc_jp":
                for second in range(256):
                    for third in range(256):
                        yield "D", prefix + bytes((0x8F, second, third)), ""
        encodable = []
        for codepoint in range(0x10000):
            if 0xD800 <= codepoint <= 0xDFFF:
                continue
            try:
                chr(codepoint).encode(codec)
                encodable.append(chr(codepoint))
            except UnicodeEncodeError:
                if codepoint % 97 == 0:
                    yield "E", prefix, text + chr(codepoint)
        for offset in range(0, len(encodable), 16):
            yield "E", prefix, text + "".join(encodable[offset:offset + 16])
        yield "E", prefix, text + "𠮷"
        generator = random.Random(codec)
        for _ in range(300):
            sample = "".join(generator.choice(encodable) for __ in range(generator.randrange(1, 40)))
            yield "D", prefix + sample.encode(codec) + bytes(generator.randrange(256) for __ in range(generator.randrange(0, 2))), ""
        # A declaration spelled through an alias still selects the frozen table.
        yield "D", f"<?xml encoding=' {codec.upper().replace('_', '-')} '?>".encode("ascii") + encodable[-1].encode(codec), ""

    prefix = b"<?xml encoding='GB18030'?>"
    for byte in range(256):
        yield "D", prefix + bytes([byte]), ""
    for first in range(256):
        for second in range(256):
            yield "D", prefix + bytes((first, second)), ""
    generator = random.Random(18030)
    for _ in range(20000):
        yield "D", prefix + bytes((generator.randrange(0x80, 0x100), generator.randrange(0x2F, 0x3B),
                                    generator.randrange(0x80, 0x100), generator.randrange(0x2F, 0x3B))), ""
    scalars = [chr(cp) for cp in range(0x80, 0x110000) if not 0xD800 <= cp <= 0xDFFF]
    for offset in range(0, len(scalars), 64):
        yield "E", prefix, prefix.decode() + "".join(scalars[offset:offset + 64])

    names = set()
    for alias, target in aliases.items():
        names.update((alias, target))
    names.update(NATIVE_ENCODINGS)
    names.update(("aliases", "mbcs", "oem", "rot13", "base64", "undefined", "windows-936", "ms_kanji"))
    generator = random.Random(20260926)
    variants = set()
    for name in names:
        variants.update((name, name.upper(), name.replace("_", "-"), name.replace("_", " "), " " + name + " ",
                         name.replace("_", ""), name.replace("_", "."), "-" + name, name + ".", name.replace("_", "--")))
    for _ in range(3000):
        variants.add("".join(generator.choice("utf8-_ .LATIN1cpSJISgbk2312eucjpBIG5") for __ in range(generator.randrange(1, 10))))
    variants.update(("\u212aoi8_r", "UTF\u00e98"))
    for name in sorted(variants):
        yield "L", name.encode("utf-8"), ""

    rng = random.Random(20260925)
    alphabet = "abcé中文𠮷\r\n\u2028<>/?=\"'"
    for _ in range(150):
        source = "<?xml version='1.0' encoding='UTF-8'?>" + ''.join(rng.choices(alphabet, k=rng.randrange(0, 90)))
        original = source.encode("utf-8")
        yield "D", original, ""
        yield "E", original, source + " addition"
    for _ in range(250):
        yield "D", bytes(rng.randrange(256) for __ in range(rng.randrange(0, 30))), ""


def main():
    samples = list(cases())
    payload = ''.join(op + original.hex() + '\t' + source.encode().hex() + '\n'
                      for op, original, source in samples)
    output = subprocess.run([sys.argv[1]], input=payload, text=True, capture_output=True, check=True).stdout.splitlines()
    assert len(output) == len(samples), (len(output), len(samples))
    for index, ((operation, original, source), actual) in enumerate(zip(samples, output)):
        if operation == "L":
            name = original.decode("utf-8")
            codec = resolvable(name)
            expected = "E" if codec is None else "S" + codec.encode().hex()
            assert actual == expected, f"lookup of {name!r}: expected {expected}, got {actual}"
            continue
        if not is_native(operation, original, source):
            assert actual == "N", f"Case {index} unexpectedly used native codec"
            continue
        try:
            expected = decode_source(original).encode() if operation == "D" else encode_source(original, source)
            expected = "S" + expected.hex()
        except (LookupError, ValueError, UnicodeError):
            expected = "E"
        assert actual == expected, (
            f"Case {index} differs: operation={operation}, original={original!r}, "
            f"source={source!r}, expected={expected!r}, actual={actual!r}"
        )
    native_count = sum(case[0] != "L" and is_native(*case) for case in samples)
    lookups = sum(case[0] == "L" for case in samples)
    print(f"{native_count} native OPF byte codec cases and {lookups} codec lookups match legacy Python; "
          f"{len(samples) - native_count - lookups} retain Python fallback")


if __name__ == "__main__":
    main()
