"""Run fixed-viewport Cmoa visual checks in Sigil's real Preview widget."""

import hashlib
import os
import pathlib
import shutil
import sys

from run_cmoa_epub_integration import EXPECTED_SAMPLE_SHA256
from run_opf_resource_integration import run


def main():
    if len(sys.argv) != 2:
        raise SystemExit("expected build directory")
    source_value = os.environ.get("SIGIL_CMOA_TEST_EPUB", "")
    source = pathlib.Path(source_value).expanduser()
    if not source_value or not source.is_file():
        print("SKIP: set SIGIL_CMOA_TEST_EPUB to the private Cmoa EPUB", file=sys.stderr)
        raise SystemExit(77)
    digest = hashlib.sha256(source.read_bytes()).hexdigest()
    if digest != EXPECTED_SAMPLE_SHA256:
        raise SystemExit(
            f"private Cmoa EPUB fingerprint mismatch: expected "
            f"{EXPECTED_SAMPLE_SHA256}, got {digest}"
        )

    def fixture(scratch):
        target = scratch / "cmoa-visual.epub"
        shutil.copy2(source, target)
        return target, digest, None

    def verify(path, expected, _members):
        if hashlib.sha256(path.read_bytes()).hexdigest() != expected:
            raise AssertionError("visual acceptance changed its disposable EPUB")
        if hashlib.sha256(source.read_bytes()).hexdigest() != digest:
            raise AssertionError("visual acceptance changed the private Cmoa EPUB")

    run(
        pathlib.Path(sys.argv[1]).resolve(),
        test_entry="cmoa_preview_visual_integration_test.cpp",
        build_fixture=fixture,
        verify=verify,
        direct_app_executable=True,
        run_timeout=45,
    )


if __name__ == "__main__":
    main()
