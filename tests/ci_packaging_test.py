import hashlib
import importlib.util
import pathlib
import re
import tempfile
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[1]
WORKFLOW = ROOT / ".github" / "workflows" / "package-enhanced.yml"
SCRIPT = ROOT / "ci_scripts" / "prepare_release_assets.py"
CHANGELOG = ROOT / "ChangeLog.txt"
VERSION_XML = ROOT / "version.xml"
README = ROOT / "README.md"


def load_release_assets_module():
    spec = importlib.util.spec_from_file_location("prepare_release_assets", SCRIPT)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


class CiPackagingTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.module = load_release_assets_module()
        cls.workflow = WORKFLOW.read_text(encoding="utf-8")

    def test_release_assets_are_validated_and_checksummed(self):
        payloads = {
            "Sigil-2.8.1E5-Windows-x64-Setup.exe": b"windows",
            "Sigil-Enhanced-2.8.1E5-macos-x64.dmg": b"mac-intel",
            "Sigil-Enhanced-2.8.1E5-macos-arm64.dmg": b"mac-arm",
        }
        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory)
            for name, payload in payloads.items():
                (root / name).write_bytes(payload)
            checksum_path = self.module.prepare_release_assets(root)
            lines = checksum_path.read_text(encoding="ascii").splitlines()
        expected = [
            "{0}  {1}".format(hashlib.sha256(payloads[name]).hexdigest(), name)
            for name in sorted(payloads)
        ]
        self.assertEqual(lines, expected)

    def test_release_assets_reject_missing_or_unexpected_files(self):
        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory)
            (root / "windows.exe").write_bytes(b"windows")
            (root / "intel.dmg").write_bytes(b"intel")
            (root / "notes.txt").write_text("unexpected", encoding="ascii")
            with self.assertRaises(self.module.ReleaseAssetError):
                self.module.prepare_release_assets(root)

    def test_tag_release_waits_for_packages_and_has_narrow_write_permission(self):
        self.assertIn("name: Validate package version", self.workflow)
        self.assertIn('tag_version="${GITHUB_REF_NAME#v}"', self.workflow)
        self.assertIn("name: Publish GitHub Release", self.workflow)
        self.assertIn("actions/download-artifact@v8", self.workflow)
        self.assertIn("python3 ci_scripts/prepare_release_assets.py release-assets", self.workflow)
        self.assertIn("gh release create", self.workflow)
        self.assertIn("gh release upload", self.workflow)
        self.assertRegex(
            self.workflow,
            r"release:\n(?:.*\n){0,8}\s+permissions:\n\s+contents: write",
        )

    def test_manual_windows_only_mode_skips_macos_packages(self):
        self.assertIn("windows_only:", self.workflow)
        self.assertIn(
            "Run only Windows package jobs. Useful for Windows CI debugging.",
            self.workflow,
        )
        self.assertIn("&& !inputs.windows_only", self.workflow)

    def test_windows_jobs_explicitly_enable_and_validate_pyside6_packaging(self):
        self.assertEqual(self.workflow.count("-DUSE_VIRT_PY=1"), 2)
        self.assertEqual(self.workflow.count("-DPACKAGE_PYSIDE6=1"), 2)
        self.assertIn("'winvirtpy.cmake'", self.workflow)

    def test_release_version_metadata_is_consistent(self):
        cmake = (ROOT / "CMakeLists.txt").read_text(encoding="utf-8")
        parts = {
            name: re.search(
                r"set\( SIGIL_{0}_VERSION ([0-9]+) \)".format(name), cmake
            ).group(1)
            for name in ("MAJOR", "MINOR", "REVISION", "ENHANCED")
        }
        release_version = "{MAJOR}.{MINOR}.{REVISION}E{ENHANCED}".format(**parts)
        update_version = release_version.replace("E", ".E")
        self.assertIn(
            "<current-version>{0}</current-version>".format(update_version),
            VERSION_XML.read_text(encoding="utf-8"),
        )
        self.assertTrue(
            CHANGELOG.read_text(encoding="utf-8").startswith(
                "Sigil-Enhanced-{0}\n".format(update_version)
            )
        )
        release_notes = (
            ROOT / "docs" / "ReleaseNotes-{0}.md".format(release_version)
        )
        self.assertTrue(release_notes.is_file())
        self.assertTrue(
            release_notes.read_text(encoding="utf-8").startswith(
                "# Sigil-Enhanced {0} 更新说明\n".format(release_version)
            )
        )
        readme = README.read_text(encoding="utf-8")
        self.assertIn(
            "当前开发版本：**{0}**".format(release_version), readme
        )
        self.assertIn(
            "docs/ReleaseNotes-{0}.md".format(release_version), readme
        )


if __name__ == "__main__":
    unittest.main()
