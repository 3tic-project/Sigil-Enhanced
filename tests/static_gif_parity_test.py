"""Compare the C++ static GIF writer with the legacy Pillow conversion via Qt readback."""

import math
import random
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
sys.path[:0] = [str(ROOT / "tests"), str(ROOT / "tests" / "fixtures")]
try:
    from PIL import Image
    from imagesupport_legacy import convert_png_to_static_gif
    from gif_legacy_readback_test import EXPECTED
except ImportError:
    Image = None

ENCODER = READER = None


def readback(gif):
    fields = subprocess.run((READER, str(gif)), capture_output=True, text=True, check=True).stdout.split()
    return int(fields[0]), int(fields[1]), int(fields[2]), [int(value, 16) for value in fields[3:]]


def native(png, gif):
    subprocess.run((ENCODER, str(png), str(gif)), check=True)
    return readback(gif)


def legacy(png, gif):
    if convert_png_to_static_gif(str(png), str(gif)) != 1:
        raise AssertionError("Pillow conversion failed")
    return readback(gif)


def psnr(original, pixels):
    error = 0
    for rgb, argb in zip(original, pixels):
        for shift, value in zip((16, 8, 0), rgb[:3]):
            error += (((argb >> shift) & 0xFF) - value) ** 2
    mean = error / (3 * len(original))
    return float("inf") if mean == 0 else 10 * math.log10(255 * 255 / mean)


@unittest.skipIf(Image is None, "Pillow is unavailable in the test interpreter")
class StaticGifParityTest(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.root = Path(self.directory.name)

    def tearDown(self):
        self.directory.cleanup()

    def save(self, name, mode, size, pixels):
        png = self.root / (name + ".png")
        image = Image.new(mode, size)
        image.putdata(pixels)
        image.save(png, "PNG")
        return png

    def both(self, name, mode, size, pixels):
        png = self.save(name, mode, size, pixels)
        return native(png, self.root / (name + ".native.gif")), legacy(png, self.root / (name + ".legacy.gif"))

    def test_frozen_legacy_readback(self):
        cases = {
            "rgb": ("RGB", (2, 2), [(255, 0, 0), (0, 255, 0), (0, 0, 255), (255, 255, 255)]),
            "rgba": ("RGBA", (3, 1), [(255, 0, 0, 0), (0, 255, 0, 128), (0, 0, 255, 255)]),
            "grayscale": ("L", (2, 1), [0, 255]),
        }
        for name, (mode, size, pixels) in cases.items():
            with self.subTest(name=name):
                png = self.save(name, mode, size, pixels)
                result = native(png, self.root / (name + ".gif"))
                self.assertEqual(" ".join([str(result[0]), str(result[1]), str(result[2])]
                                          + ["%08X" % value for value in result[3]]), EXPECTED[name])

    def test_exact_palettes_match_pillow(self):
        generator = random.Random(20260925)
        cases = [
            ("single", "RGB", (1, 1), [(12, 34, 56)]),
            ("two", "RGB", (5, 3), [(0, 0, 0), (255, 255, 255)] * 7 + [(0, 0, 0)]),
            ("gray", "L", (16, 16), list(range(256))),
            ("alpha-one-colour", "RGBA", (4, 4), [(9, 8, 7, 0)] * 6 + [(200, 100, 50, 255)] * 5 + [(1, 2, 3, 77)] * 5),
        ]
        for count, size in ((3, (7, 5)), (17, (64, 48)), (200, (300, 200)), (256, (97, 131))):
            palette = [(generator.randrange(256), generator.randrange(256), generator.randrange(256)) for _ in range(count)]
            pixels = [palette[index % count] for index in range(count)]
            pixels += [generator.choice(palette) for _ in range(size[0] * size[1] - count)]
            cases.append(("colours-%d" % count, "RGB", size, pixels))
        for name, mode, size, pixels in cases:
            with self.subTest(name=name):
                ours, theirs = self.both(name, mode, size, pixels)
                self.assertEqual(ours, theirs)

    def test_small_rgba_palette_stays_exact(self):
        # Pillow quantizes RGBA with a lossy octree even below 256 colours;
        # the C++ writer keeps them exact and marks the same pixels transparent.
        generator = random.Random(20260925)
        palette = [(generator.randrange(256), generator.randrange(256), generator.randrange(256), 255) for _ in range(120)]
        pixels = [generator.choice(palette + [(0, 0, 0, 0)]) for _ in range(150 * 90)]
        ours, theirs = self.both("alpha-colours", "RGBA", (150, 90), pixels)
        self.assertEqual(ours[:3], theirs[:3])
        self.assertEqual([value >> 24 for value in ours[3]], [value >> 24 for value in theirs[3]])
        expected = [0 if a == 0 else (0xFF << 24) | (r << 16) | (g << 8) | b for r, g, b, a in pixels]
        self.assertEqual(ours[3], expected)
        self.assertGreater(psnr(pixels, ours[3]), psnr(pixels, theirs[3]))

    def test_quantized_images_are_as_close_as_pillow(self):
        width, height = 256, 192
        gradient = [(x, y * 255 // (height - 1), (x * 7 + y * 3) % 256) for y in range(height) for x in range(width)]
        generator = random.Random(7)
        noise = [(generator.randrange(256), generator.randrange(256), generator.randrange(256)) for _ in range(120 * 80)]
        for name, size, pixels in (("gradient", (width, height), gradient), ("noise", (120, 80), noise)):
            with self.subTest(name=name):
                ours, theirs = self.both(name, "RGB", size, pixels)
                self.assertEqual(ours[:3], theirs[:3])
                self.assertGreaterEqual(psnr(pixels, ours[3]), psnr(pixels, theirs[3]) - 1.0)

    def test_every_transparent_pixel_stays_transparent(self):
        # Pillow keeps only its first alpha-0 palette colour transparent; the
        # C++ writer treats every alpha-0 pixel as transparent.
        pixels = [(255, 0, 0, 0), (0, 255, 0, 0), (0, 0, 255, 255), (0, 0, 0, 0)]
        png = self.save("mixed-transparent", "RGBA", (2, 2), pixels)
        width, height, alpha, argb = native(png, self.root / "mixed.gif")
        self.assertEqual((width, height, alpha), (2, 2, 1))
        self.assertEqual([value >> 24 for value in argb], [0, 0, 0xFF, 0])
        self.assertEqual(argb[2], 0xFF0000FF)


if __name__ == "__main__":
    ENCODER, READER = sys.argv[1], sys.argv[2]
    unittest.main(argv=[sys.argv[0]])
