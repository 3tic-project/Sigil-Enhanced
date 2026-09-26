"""Freeze Qt's visual readback of GIFs written by the old Pillow path."""

import subprocess
import sys
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tests" / "fixtures"))
try:
    from PIL import Image
    from imagesupport_legacy import convert_png_to_static_gif
except ImportError:
    Image = None


@unittest.skipIf(Image is None, "Pillow is unavailable in the test interpreter")
class GifLegacyReadbackTest(unittest.TestCase):
    def test_visual_readback(self):
        cases = {
            "rgb": ("RGB", (2, 2), [
                (255, 0, 0), (0, 255, 0), (0, 0, 255), (255, 255, 255),
            ]),
            "rgba": ("RGBA", (3, 1), [
                (255, 0, 0, 0), (0, 255, 0, 128), (0, 0, 255, 255),
            ]),
            "grayscale": ("L", (2, 1), [0, 255]),
        }
        with tempfile.TemporaryDirectory() as directory:
            for name, (mode, size, pixels) in cases.items():
                with self.subTest(name=name):
                    png = Path(directory) / f"{name}.png"
                    gif = Path(directory) / f"{name}.gif"
                    image = Image.new(mode, size)
                    image.putdata(pixels)
                    image.save(png, "PNG")
                    self.assertEqual(convert_png_to_static_gif(str(png), str(gif)), 1)
                    output = subprocess.run(
                        (sys.argv[1], str(gif)), capture_output=True,
                        text=True, check=True,
                    ).stdout.strip()
                    self.assertEqual(output, EXPECTED[name])


EXPECTED = {
    "rgb": "2 2 0 FFFF0000 FF00FF00 FF0000FF FFFFFFFF",
    "rgba": "3 1 1 00FF0000 FF00FF00 FF0000FF",
    "grayscale": "2 1 0 FF000000 FFFFFFFF",
}


if __name__ == "__main__":
    unittest.main(argv=[sys.argv[0]])
