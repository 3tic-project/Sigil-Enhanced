import importlib.util
import json
import pathlib
import tempfile
import unittest


ROOT = pathlib.Path(__file__).parents[1]
SCRIPT = ROOT / "src" / "Resource_Files" / "python_pkg" / "sync_python_packages.py"
SPEC = importlib.util.spec_from_file_location("sync_python_packages", SCRIPT)
MODULE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(MODULE)
VERIFY_SCRIPT = ROOT / "ci_scripts" / "verify_bundled_python.py"
VERIFY_SPEC = importlib.util.spec_from_file_location(
    "verify_bundled_python", VERIFY_SCRIPT
)
VERIFY_MODULE = importlib.util.module_from_spec(VERIFY_SPEC)
VERIFY_SPEC.loader.exec_module(VERIFY_MODULE)


class PythonPackageSyncTest(unittest.TestCase):
    def test_platform_requirements_share_the_complete_core_lock(self):
        package_root = ROOT / "src" / "Resource_Files" / "python_pkg"
        core = (package_root / "requirements-core.txt").read_text(
            encoding="utf-8"
        ).splitlines()
        windows = (package_root / "winreqs.txt").read_text(
            encoding="utf-8"
        ).splitlines()
        appimage = (ROOT / ".github" / "workflows" / "requirements.txt").read_text(
            encoding="utf-8"
        ).splitlines()
        self.assertEqual(windows[:-1], core)
        self.assertEqual(windows[-1], "PySide6==${QTVER}")
        self.assertEqual(appimage[:-1], core)
        self.assertEqual(appimage[-1], "PySide6==6.10.2")
        self.assertIn("mcp==1.28.1", core)
        self.assertIn("cryptography==48.0.0", core)
        self.assertTrue(all("==" in requirement for requirement in core))

    def test_isolated_runtime_verifier_checks_metadata_version_and_import_path(self):
        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory)
            package = root / "fixture_package"
            package.mkdir()
            (package / "__init__.py").write_text("VALUE = 1\n", encoding="utf-8")
            metadata = root / "fixture_package-1.2.3.dist-info"
            metadata.mkdir()
            (metadata / "METADATA").write_text(
                "Metadata-Version: 2.1\nName: fixture-package\nVersion: 1.2.3\n",
                encoding="utf-8",
            )
            requirements = root / "requirements.txt"
            requirements.write_text("fixture-package==1.2.3\n", encoding="utf-8")
            self.assertEqual(
                VERIFY_MODULE.verify(requirements, [root]),
                1,
            )

    def test_copy_all_preserves_builtins_and_removes_stale_synced_paths(self):
        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory)
            cache = root / "cache"
            destination = root / "destination"
            (cache / "pure_package").mkdir(parents=True)
            (cache / "pure_package" / "__init__.py").write_text("VALUE = 1\n")
            (cache / "native_package").mkdir()
            (cache / "native_package" / "core.so").write_bytes(b"native")
            (cache / "example-1.0.dist-info").mkdir()
            (cache / "example-1.0.dist-info" / "METADATA").write_text("Name: example\n")
            (cache / "bin").mkdir()
            (cache / "bin" / "example").write_text("ignored")

            destination.mkdir()
            (destination / "sigil_builtin.py").write_text("keep = True\n")
            (destination / "stale_package").mkdir()
            manifest = destination / MODULE.COPY_ALL_MANIFEST
            manifest.write_text(json.dumps({"paths": ["stale_package"]}))

            self.assertEqual(MODULE.sync_all_packages(cache, destination), 0)
            self.assertTrue((destination / "sigil_builtin.py").is_file())
            self.assertFalse((destination / "stale_package").exists())
            self.assertEqual(
                (destination / "pure_package" / "__init__.py").read_text(),
                "VALUE = 1\n",
            )
            self.assertEqual(
                (destination / "native_package" / "core.so").read_bytes(),
                b"native",
            )
            self.assertTrue((destination / "example-1.0.dist-info" / "METADATA").is_file())
            self.assertFalse((destination / "bin").exists())

            payload = json.loads(manifest.read_text())
            self.assertEqual(
                payload["paths"],
                ["example-1.0.dist-info", "native_package", "pure_package"],
            )

    def test_copy_all_replaces_changed_package_directory(self):
        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory)
            cache = root / "cache"
            destination = root / "destination"
            (cache / "package").mkdir(parents=True)
            (cache / "package" / "new.py").write_text("new = True\n")
            (destination / "package").mkdir(parents=True)
            (destination / "package" / "old.py").write_text("old = True\n")
            (destination / MODULE.COPY_ALL_MANIFEST).write_text(
                json.dumps({"paths": ["package"]})
            )

            MODULE.sync_all_packages(cache, destination)
            self.assertFalse((destination / "package" / "old.py").exists())
            self.assertTrue((destination / "package" / "new.py").is_file())


if __name__ == "__main__":
    unittest.main()
