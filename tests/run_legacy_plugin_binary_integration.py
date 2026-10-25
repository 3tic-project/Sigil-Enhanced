"""Check legacy plugins that overwrite binary resources without bk.writefile()."""

import argparse
import os
import pathlib
import struct
import zipfile
import zlib

from run_main_window_noop_integration import epub2_fixture
from run_opf_resource_integration import epub_fixture, run


SCENARIOS = (
    "noop", "identical", "touch", "compress", "same-metadata", "binary", "api-write",
    "failed", "crashed", "cancelled",
)

PLUGIN = '''import os
import pathlib
import sys
import time


def run(bk):
    scenario = os.environ["SIGIL_BINARY_SCENARIO"]
    target_id = "blob" if scenario == "binary" else "cover"
    target = pathlib.Path(bk._w.ebook_root) / bk.id_to_bookpath(target_id)
    original = target.read_bytes()
    info = target.stat()
    if scenario == "noop":
        return 0
    if scenario == "touch":
        os.utime(target, ns=(info.st_atime_ns, info.st_mtime_ns + 2000000000))
        return 0
    if scenario == "identical":
        target.write_bytes(original)
        return 0
    if scenario == "compress-plugin":
        sys.path.insert(0, os.environ["SIGIL_COMPRESS_IMG_PLUGIN_DIR"])
        from utils import compress, posprocess
        compress.TEMPDIR = bk._w.outdir
        args = dict(ROTATE=180, CONV=None, CONV_RATE=None, CONV_LV=None,
                    J_RATE=None, W_RATE=None, DEPTH=None, ULOSS=None,
                    ULOSS_LV=None, FORMAT="PNG", OPERATE=1)
        log = compress.ImagesProcess(str(target), args)
        posprocess.LOG.append(log)
        posprocess.PosProcess(bk)
        expected = pathlib.Path(log["OUT"]).read_bytes()
        assert len(expected) < len(original), "Real plugin did not compress the image"
    else:
        name = "same-size.png" if scenario == "same-metadata" else "compressed.png"
        expected = pathlib.Path(__file__).with_name(name).read_bytes()
        if scenario == "api-write":
            bk.writefile("cover", expected)
        else:
            # Same overwrite path as SigilCompressImg.utils.posprocess.overwrite.
            with target.open("wb") as stream:
                stream.write(expected)
        if scenario == "same-metadata":
            assert len(expected) == len(original)
            os.utime(target, ns=(info.st_atime_ns, info.st_mtime_ns))
    pathlib.Path(os.environ["SIGIL_BINARY_EXPECTED_PATH"]).write_bytes(expected)
    if scenario == "failed":
        return -1
    if scenario == "crashed":
        import signal
        os.kill(os.getpid(), signal.SIGKILL)
    if scenario == "cancelled":
        print("DIRECT_BINARY_WRITE_FINISHED", flush=True)
        time.sleep(30)
    return 0
'''


def png(level, red=80):
    def chunk(kind, data):
        return (struct.pack(">I", len(data)) + kind + data
                + struct.pack(">I", zlib.crc32(kind + data)))

    pixels = (b"\0" + bytes((red, 100, 120)) * 64) * 64
    return (b"\x89PNG\r\n\x1a\n"
            + chunk(b"IHDR", struct.pack(">IIBBBBB", 64, 64, 8, 2, 0, 0, 0))
            + chunk(b"IDAT", zlib.compress(pixels, level)) + chunk(b"IEND", b""))


def fixture(version):
    def build(scratch):
        path, source, members = (epub2_fixture if version == "epub2" else epub_fixture)(scratch)
        encoding = "utf-16" if version == "epub3" else "utf-8"
        opf = members["OEBPS/content.opf"].decode(encoding)
        opf = opf.replace("</manifest>", "<item id='cover' href='Images/cover.png' "
                          "media-type='image/png'/><item id='blob' href='Misc/blob.bin' "
                          "media-type='application/octet-stream'/></manifest>")
        # The legacy Python OPF parser accepts UTF-8 only.
        members["OEBPS/content.opf"] = opf.replace("UTF-16", "UTF-8").encode("utf-8")
        members["OEBPS/Images/cover.png"] = png(0)
        members["OEBPS/Misc/blob.bin"] = b"original binary resource"
        with zipfile.ZipFile(path, "w") as archive:
            for name, data in members.items():
                archive.writestr(name, data)
        plugin = scratch / "plugins" / "DirectBinaryFixture"
        plugin.mkdir(parents=True)
        (plugin / "plugin.xml").write_text('''<plugin>
<name>DirectBinaryFixture</name><type>edit</type><author>Sigil tests</author>
<description>Direct binary write regression</description><engine>python3.4</engine>
<version>1.0</version><oslist>win,osx,unx</oslist>
<autostart>true</autostart><autoclose>false</autoclose>
</plugin>''', encoding="utf-8")
        (plugin / "plugin.py").write_text(PLUGIN, encoding="utf-8")
        (plugin / "compressed.png").write_bytes(png(9))
        (plugin / "same-size.png").write_bytes(png(0, red=180))
        return path, source, members

    return build


def verify_scenario(path, scenario, members):
    expected_path = pathlib.Path(str(path) + ".expected")
    if scenario in ("noop", "identical", "touch"):
        assert not expected_path.exists()
        return
    expected = expected_path.read_bytes()
    target = "OEBPS/Misc/blob.bin" if scenario == "binary" else "OEBPS/Images/cover.png"
    for output in (path, pathlib.Path(str(path) + ".copy.epub")):
        with zipfile.ZipFile(output) as archive:
            assert archive.read(target) == expected
            for name, data in members.items():
                if name not in ("OEBPS/content.opf", target):
                    assert archive.read(name) == data, "Changed unrelated file: " + name
    print("Saved EPUB and Save A Copy contain the updated binary:", scenario)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("build", type=pathlib.Path)
    parser.add_argument("--fixture", choices=("epub2", "epub3"), default="epub3")
    parser.add_argument("--scenario", choices=SCENARIOS + ("compress-plugin", "all"), default="all")
    parser.add_argument("--compress-plugin-dir", type=pathlib.Path)
    args = parser.parse_args()
    scenarios = SCENARIOS if args.scenario == "all" else (args.scenario,)
    if args.compress_plugin_dir:
        os.environ["SIGIL_COMPRESS_IMG_PLUGIN_DIR"] = str(args.compress_plugin_dir.resolve())
        os.environ["PYTHONDONTWRITEBYTECODE"] = "1"
        if args.scenario == "all":
            scenarios += ("compress-plugin",)
    elif args.scenario == "compress-plugin":
        parser.error("--scenario compress-plugin requires --compress-plugin-dir")

    def verify(path, _source, members):
        for scenario in scenarios:
            verify_scenario(pathlib.Path(str(path) + "." + scenario + ".epub"), scenario, members)

    run(args.build.resolve(), "legacy_plugin_binary_integration_test.cpp",
        fixture(args.fixture), verify, direct_app_executable=True,
        run_arguments=[(scenario,) for scenario in scenarios])
