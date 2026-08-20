import ast
import importlib.util
import json
import pathlib
import sys
import tempfile
import textwrap
import unittest
import warnings


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
        windows_runtime = (package_root / "requirements-windows.txt").read_text(
            encoding="utf-8"
        ).splitlines()
        appimage = (ROOT / ".github" / "workflows" / "requirements.txt").read_text(
            encoding="utf-8"
        ).splitlines()
        windows_only = {"colorama==0.4.6", "pywin32==312"}
        self.assertEqual(
            [
                requirement
                for requirement in windows_runtime
                if requirement not in windows_only
            ],
            core,
        )
        self.assertTrue(windows_only.issubset(windows_runtime))
        self.assertEqual(windows[:-1], windows_runtime)
        self.assertEqual(windows[-1], "PySide6==${QTVER}")
        self.assertEqual(appimage[:-1], core)
        self.assertEqual(appimage[-1], "PySide6==6.10.2")
        self.assertIn("mcp==1.28.1", core)
        self.assertIn("cryptography==48.0.0", core)
        self.assertIn("beautifulsoup4==4.13.4", core)
        self.assertIn("soupsieve==2.7", core)
        self.assertTrue(all("==" in requirement for requirement in core))

    def test_windows_package_sync_preserves_the_curated_pyside_runtime(self):
        cmake = (ROOT / "src" / "qt6sigil.cmake").read_text(encoding="utf-8")
        appimage = (
            ROOT / ".github" / "workflows" / "build_sigil_appimage.sh"
        ).read_text(encoding="utf-8")
        gather = (
            ROOT
            / "src"
            / "Resource_Files"
            / "python_pkg"
            / "windows_python_gather6.py"
        ).read_text(encoding="utf-8")
        paths_template = (
            ROOT
            / "src"
            / "Resource_Files"
            / "python_pkg"
            / "python_paths6.py"
        ).read_text(encoding="utf-8")

        sync = cmake.index("Syncing complete Python dependency tree into Windows package")
        gather_copy = cmake.rindex("windows_python_gather6.py", 0, sync)
        self.assertLess(gather_copy, sync)
        self.assertIn(
            "set( SIGIL_WINDOWS_PYTHON_SITE_PACKAGES ${MAIN_PACKAGE_DIR}/Lib/site-packages )",
            cmake,
        )
        self.assertIn("--dest ${SIGIL_WINDOWS_PYTHON_SITE_PACKAGES}", cmake)
        self.assertIn("--site-packages ${SIGIL_WINDOWS_PYTHON_SITE_PACKAGES}", cmake)
        self.assertIn("lib_dir = os.path.join(tmp_prefix, 'Lib')", gather)
        appimage_sync = appimage.index("sync_python_packages.py")
        appimage_gather = appimage.index("appimg_python3_gather.py")
        self.assertLess(appimage_sync, appimage_gather)
        self.assertIn("if ( PACKAGE_PYSIDE6 )", cmake)
        self.assertIn("Bundle PySide6 with the Windows package", (ROOT / "CMakeLists.txt").read_text(encoding="utf-8"))
        self.assertIn("required_site_packages", gather)
        self.assertIn("Required bundled Python package not found", gather)
        self.assertIn("def ensure_pyside6():", gather)
        self.assertIn("'PySide6=={0}'.format(pyside6_version)", gather)
        self.assertIn("The synchronized dependency tree already", gather)
        self.assertIn("if pkg in ('PySide6', 'shiboken6'):", gather)
        self.assertIn("pyside6_version = '${QTVER}'", paths_template)
        self.assertIn("sys_dlls = r'${SYS_DLL_DIR}'", paths_template)

    def test_windows_venv_repairs_incomplete_pyside6_cache_entries(self):
        cmake = (ROOT / "winvirtpy.cmake").read_text(encoding="utf-8")

        self.assertIn('import PySide6, shiboken6', cmake)
        self.assertIn('Repairing cached virtual Python environment missing PySide6.', cmake)
        self.assertIn('PySide6 is unavailable after installing', cmake)

    def test_windows_python_paths_template_preserves_backslashes(self):
        template = (
            ROOT
            / "src"
            / "Resource_Files"
            / "python_pkg"
            / "python_paths6.py"
        ).read_text(encoding="utf-8")
        replacements = {
            "${USE_NEWER_FINDPYTHON3}": "1",
            "${Python3_EXECUTABLE}": r"D:\a\Sigil-Enhanced\sigilpy\Scripts\python.exe",
            "${Python3_LIBRARIES}": r"D:\a\Sigil-Enhanced\python314.lib",
            "${Python3_INCLUDE_DIRS}": r"D:\a\Sigil-Enhanced\include",
            "${PYTHON_EXECUTABLE}": r"D:\Python\python.exe",
            "${PYTHON_LIBRARIES}": r"D:\Python\python314.lib",
            "${PYTHON_INCLUDE_DIRS}": r"D:\Python\include",
            "${QTVER}": "6.10.2",
            "${SYS_DLL_DIR}": r"C:\Windows\System32",
            "${PYTHON_DEST_DIR}": r"D:\a\Sigil-Enhanced\temp_folder",
            "${MAIN_PACKAGE_DIR}": r"D:\a\Sigil-Enhanced\temp_folder",
            "${PROJECT_NAME}": "Sigil",
            "${CMAKE_BINARY_DIR}": r"D:\a\Sigil-Enhanced\build",
            "${PACKAGE_PYSIDE6}": "1",
        }
        rendered = template
        for token, value in replacements.items():
            rendered = rendered.replace(token, value)

        with warnings.catch_warnings(record=True) as caught:
            warnings.simplefilter("always", SyntaxWarning)
            namespace = {}
            exec(compile(rendered, "python_paths6.py", "exec"), namespace)

        self.assertEqual(namespace["sys_dlls"], r"C:\Windows\System32")
        self.assertFalse(any(issubclass(item.category, SyntaxWarning) for item in caught))

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

    def test_isolated_runtime_verifier_processes_pywin32_pth_path(self):
        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory)
            nested = root / "win32" / "lib"
            nested.mkdir(parents=True)
            (nested / "pywintypes.py").write_text("VALUE = 1\n", encoding="utf-8")
            metadata = root / "pywin32-312.dist-info"
            metadata.mkdir()
            (metadata / "METADATA").write_text(
                "Metadata-Version: 2.1\nName: pywin32\nVersion: 312\n",
                encoding="utf-8",
            )
            (root / "pywin32.pth").write_text("win32/lib\n", encoding="utf-8")
            requirements = root / "requirements.txt"
            requirements.write_text("pywin32==312\n", encoding="utf-8")

            self.assertEqual(VERIFY_MODULE.verify(requirements, [root]), 1)
            sys.modules.pop("pywintypes", None)

    def test_isolated_runtime_verifier_rejects_pth_path_escape(self):
        with tempfile.TemporaryDirectory() as directory:
            base = pathlib.Path(directory)
            root = base / "site-packages"
            outside = base / "outside"
            root.mkdir()
            outside.mkdir()
            (root / "escape.pth").write_text("../outside\n", encoding="utf-8")
            requirements = root / "requirements.txt"
            requirements.write_text("", encoding="utf-8")

            with self.assertRaisesRegex(RuntimeError, "outside the package directories"):
                VERIFY_MODULE.verify(requirements, [root])

    def test_generated_windows_site_module_processes_pth_files(self):
        gather = (
            ROOT
            / "src"
            / "Resource_Files"
            / "python_pkg"
            / "windows_python_gather6.py"
        )
        tree = ast.parse(gather.read_text(encoding="utf-8"))
        candidates = [
            textwrap.dedent(node.value)
            for node in ast.walk(tree)
            if isinstance(node, ast.Constant)
            and isinstance(node.value, str)
            and "def addsitedir(sitedir):" in node.value
        ]
        self.assertEqual(len(candidates), 1)
        source = candidates[0].rsplit("if not sys.flags.no_site:", 1)[0]
        namespace = {}
        exec(compile(source, "generated-site.py", "exec"), namespace)

        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory)
            nested = root / "win32" / "lib"
            nested.mkdir(parents=True)
            (nested / "generated_site_fixture.py").write_text(
                "VALUE = 1\n", encoding="utf-8"
            )
            (nested / "generated_site_bootstrap.py").write_text(
                "BOOTSTRAPPED = True\n", encoding="utf-8"
            )
            (root / "fixture.pth").write_text(
                "win32/lib\nimport generated_site_bootstrap\n", encoding="utf-8"
            )
            original_sys_path = list(sys.path)
            try:
                namespace["addsitedir"](str(root))
                module = __import__("generated_site_fixture")
                self.assertEqual(module.VALUE, 1)
                self.assertTrue(sys.modules["generated_site_bootstrap"].BOOTSTRAPPED)
            finally:
                sys.path[:] = original_sys_path
                sys.modules.pop("generated_site_fixture", None)
                sys.modules.pop("generated_site_bootstrap", None)

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
