"""Kill a real Live transaction mid-commit and recover its pre-commit book."""

import hashlib
import os
import pathlib
import signal
import sys
import zipfile

from run_opf_resource_integration import epub_fixture, run


def main():
    if len(sys.argv) != 2:
        raise SystemExit("expected build directory")
    build = pathlib.Path(sys.argv[1]).resolve()
    expected_input_hash = None
    scratch_path = None

    def fixture(scratch):
        nonlocal expected_input_hash, scratch_path
        scratch_path = scratch
        path, source, members = epub_fixture(scratch)
        expected_input_hash = hashlib.sha256(path.read_bytes()).hexdigest()
        # The child writes this only after the first physical resource addition.
        os.environ["SIGIL_TEST_MUTATION_MARKER"] = str(scratch / "mutation-marker")
        return path, source, members

    def verify(path, _source, members):
        assert hashlib.sha256(path.read_bytes()).hexdigest() == expected_input_hash, (
            "SIGKILL changed the original EPUB")
        marker = scratch_path / "mutation-marker"
        checkpoint, work_root_text = marker.read_text().splitlines()
        work_root = pathlib.Path(work_root_text).resolve()
        assert work_root.parent == (scratch_path / "workspace").resolve()
        assert work_root.name.startswith("Sigil-")
        assert (work_root / "OEBPS/partial.css").is_file(), (
            "The host was not killed after a physical resource addition")
        repo_home = scratch_path / "repo"
        assert (repo_home / f"epub_{checkpoint}" / ".git").is_dir(), (
            "The required pre-commit checkpoint was not committed")

        sys.path.insert(0, str(build / "bin/Sigil.app/Contents/python3lib"))
        import repomanager

        tags = repomanager.get_tag_list(str(repo_home), checkpoint)
        assert len(tags) == 1, f"Expected one recovery checkpoint, got {tags}"
        tag = tags[0].split("|", 1)[0]
        recovered = pathlib.Path(repomanager.generate_epub_from_tag(
            str(repo_home), checkpoint, tag, "recovered", str(scratch_path)))
        assert recovered.is_file(), "The recovery checkout did not generate an EPUB"
        with zipfile.ZipFile(recovered) as archive:
            assert set(archive.namelist()) == set(members), (
                "The recovered book has missing or partial members")
            assert archive.read("OEBPS/content.opf") == members["OEBPS/content.opf"]
            assert archive.read("OEBPS/nav.xhtml") == members["OEBPS/nav.xhtml"]
            assert archive.read("OEBPS/a.xhtml") == members["OEBPS/a.xhtml"].replace(
                b"Original paragraph", b"unsaved before crash")
            assert "OEBPS/partial.css" not in archive.namelist()
        print("SIGKILL recovery restored the unsaved pre-commit book without partial files")

    run(build,
        test_entry="plugin_session_crash_recovery_integration_test.cpp",
        build_fixture=fixture,
        verify=verify,
        direct_app_executable=True,
        run_timeout=45,
        expected_returncode=-signal.SIGKILL)


if __name__ == "__main__":
    main()
