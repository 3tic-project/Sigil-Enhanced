import contextlib
import io
import json
import os
import pathlib
import subprocess
import tempfile
import unittest
import zipfile


ROOT = pathlib.Path(__file__).resolve().parents[1]
PYTHON_ROOT = ROOT / "src" / "Resource_Files" / "python3lib"
import sys

sys.path.insert(0, str(PYTHON_ROOT))

from sigil_kfx_import import worker


def make_epub(path: pathlib.Path) -> None:
    container = b'''<?xml version="1.0"?>
<container xmlns="urn:oasis:names:tc:opendocument:xmlns:container">
  <rootfiles><rootfile full-path="OEBPS/content.opf"/></rootfiles>
</container>'''
    opf = b'''<?xml version="1.0"?>
<package xmlns="http://www.idpf.org/2007/opf" version="3.0">
  <metadata xmlns:dc="http://purl.org/dc/elements/1.1/">
    <dc:title>Fixture</dc:title><dc:creator>Author</dc:creator><dc:language>en</dc:language>
  </metadata>
  <manifest>
    <item id="nav" href="nav.xhtml" media-type="application/xhtml+xml" properties="nav"/>
    <item id="chapter" href="chapter.xhtml" media-type="application/xhtml+xml"/>
  </manifest>
  <spine><itemref idref="chapter"/></spine>
</package>'''
    with zipfile.ZipFile(path, "w") as archive:
        archive.writestr("mimetype", b"application/epub+zip", compress_type=zipfile.ZIP_STORED)
        archive.writestr("META-INF/container.xml", container)
        archive.writestr("OEBPS/content.opf", opf)
        archive.writestr("OEBPS/nav.xhtml", b"<html xmlns='http://www.w3.org/1999/xhtml'><body/></html>")
        archive.writestr("OEBPS/chapter.xhtml", b"<html xmlns='http://www.w3.org/1999/xhtml'><body>Text</body></html>")


class KfxWorkerTest(unittest.TestCase):
    def test_isolated_bootstrap_ignores_pythonpath(self):
        bootstrap = PYTHON_ROOT / "sigil_kfx_import" / "bootstrap.py"
        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory)
            source = root / "invalid.kfx"
            source.write_bytes(b"not-kfx")
            output = root / "output.epub"
            poison = root / "poison"
            poison.mkdir()
            (poison / "sigil_kfx_import.py").write_text(
                "raise RuntimeError('PYTHONPATH was not isolated')\n",
                encoding="utf-8",
            )
            environment = dict(os.environ)
            environment["PYTHONPATH"] = str(poison)
            process = subprocess.run(
                [
                    sys.executable,
                    "-I",
                    "-S",
                    str(bootstrap),
                    "--input",
                    str(source),
                    "--output",
                    str(output),
                ],
                env=environment,
                check=False,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                text=True,
            )
        events = [json.loads(line) for line in process.stdout.splitlines()]
        self.assertEqual(process.returncode, 2)
        self.assertEqual(events[-1]["code"], "KFX-E-INPUT")
        self.assertNotIn("PYTHONPATH was not isolated", process.stderr)

    def test_generated_epub_gate_reports_structure(self):
        with tempfile.TemporaryDirectory() as directory:
            path = pathlib.Path(directory) / "fixture.epub"
            make_epub(path)
            summary = worker.validate_epub(path)
            self.assertEqual(summary["title"], "Fixture")
            self.assertEqual(summary["authors"], ["Author"])
            self.assertEqual(summary["spineItems"], 1)

    def test_generated_epub_gate_rejects_compressed_mimetype(self):
        with tempfile.TemporaryDirectory() as directory:
            path = pathlib.Path(directory) / "invalid.epub"
            with zipfile.ZipFile(path, "w") as archive:
                archive.writestr(
                    "mimetype",
                    b"application/epub+zip",
                    compress_type=zipfile.ZIP_DEFLATED,
                )
            with self.assertRaisesRegex(worker.WorkerError, "uncompressed"):
                worker.validate_epub(path)

    def test_preflight_rejects_archive_path_traversal(self):
        with tempfile.TemporaryDirectory() as directory:
            path = pathlib.Path(directory) / "unsafe.kfx-zip"
            with zipfile.ZipFile(path, "w") as archive:
                archive.writestr("../escape.kfx", b"CONT")
            with self.assertRaisesRegex(worker.WorkerError, "unsafe path"):
                worker.preflight_input(path)

    def test_protocol_events_are_single_line_json(self):
        stream = io.StringIO()
        with contextlib.redirect_stdout(stream):
            worker.emit("phase", name="parse")
        lines = stream.getvalue().splitlines()
        self.assertEqual(len(lines), 1)
        self.assertEqual(json.loads(lines[0]), {"protocol": 1, "event": "phase", "name": "parse"})


if __name__ == "__main__":
    unittest.main()
