import pathlib
import re
import subprocess
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[1]
HARFBUZZ = ROOT / "3rdparty" / "harfbuzz"


class VendoredSourceManifestTest(unittest.TestCase):
    def test_harfbuzz_cmake_inputs_exist(self):
        for relative_path in (
            "CMakeLists.txt",
            "meson.build",
            "src/harfbuzz.cc",
            "src/hb-subset.cc",
            "src/hb-version.h",
        ):
            with self.subTest(path=relative_path):
                self.assertTrue((HARFBUZZ / relative_path).is_file())

    def test_harfbuzz_cmake_inputs_are_tracked_in_a_git_checkout(self):
        if not (ROOT / ".git").exists():
            self.skipTest("source archive has no Git index")
        for relative_path in (
            "3rdparty/harfbuzz/CMakeLists.txt",
            "3rdparty/harfbuzz/meson.build",
            "3rdparty/harfbuzz/src/harfbuzz.cc",
            "3rdparty/harfbuzz/src/hb-subset.cc",
            "3rdparty/harfbuzz/src/hb-version.h",
        ):
            with self.subTest(path=relative_path):
                result = subprocess.run(
                    ["git", "ls-files", "--error-unmatch", "--", relative_path],
                    cwd=ROOT,
                    stdout=subprocess.DEVNULL,
                    stderr=subprocess.DEVNULL,
                    check=False,
                )
                self.assertEqual(result.returncode, 0)

    def test_harfbuzz_version_metadata_is_consistent(self):
        meson = (HARFBUZZ / "meson.build").read_text(encoding="utf-8")
        wrapper = (ROOT / "3rdparty" / "cmake" / "harfbuzz.cmake").read_text(
            encoding="utf-8"
        )
        header = (HARFBUZZ / "src" / "hb-version.h").read_text(encoding="utf-8")
        readme = (HARFBUZZ / "README.sigil.md").read_text(encoding="utf-8")

        meson_version = re.search(r"version: '([0-9]+\.[0-9]+\.[0-9]+)'", meson)
        wrapper_version = re.search(
            r'SIGIL_BUNDLED_HARFBUZZ_VERSION "([0-9]+\.[0-9]+\.[0-9]+)"',
            wrapper,
        )
        header_version = re.search(
            r'#define HB_VERSION_STRING "([0-9]+\.[0-9]+\.[0-9]+)"', header
        )
        self.assertIsNotNone(meson_version)
        self.assertIsNotNone(wrapper_version)
        self.assertIsNotNone(header_version)

        expected = meson_version.group(1)
        self.assertEqual(wrapper_version.group(1), expected)
        self.assertEqual(header_version.group(1), expected)
        self.assertIn("Upstream version: {0}".format(expected), readme)

    def test_vendored_compiler_flags_do_not_leak_to_other_languages(self):
        opencc = (ROOT / "3rdparty" / "opencc" / "CMakeLists.txt").read_text(
            encoding="utf-8"
        )
        hunspell = (ROOT / "3rdparty" / "cmake" / "hunspell.cmake").read_text(
            encoding="utf-8"
        )

        self.assertIn('$<$<COMPILE_LANGUAGE:CXX>:/W4>', opencc)
        for definitions in re.findall(
            r"add_definitions\((.*?)\)", opencc, flags=re.DOTALL
        ):
            self.assertNotIn("/W4", definitions)
        self.assertNotIn("set(CMAKE_CXX_FLAGS", hunspell)
        self.assertIn(
            "target_compile_features(${PROJECT_NAME} PRIVATE cxx_std_11)",
            hunspell,
        )


if __name__ == "__main__":
    unittest.main()
