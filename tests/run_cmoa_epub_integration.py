"""Run the native Cmoa acceptance test against a private EPUB fixture.

Set SIGIL_CMOA_TEST_EPUB to the fixture path.  A missing fixture returns 77 so
CTest reports the private acceptance test as skipped rather than passed.
"""

import hashlib
import os
import pathlib
import shutil
import sys

from run_opf_resource_integration import run


EXPECTED_SAMPLE_SHA256 = "cc95eccdc564e3a55b076966d99116dc7690ca8b88058cb6af8951b7b95179a9"


def main():
    if len(sys.argv) != 2:
        raise SystemExit("expected build directory")
    source_value = os.environ.get("SIGIL_CMOA_TEST_EPUB", "")
    source = pathlib.Path(source_value).expanduser()
    if not source_value or not source.is_file():
        print("SKIP: set SIGIL_CMOA_TEST_EPUB to the private Cmoa EPUB", file=sys.stderr)
        raise SystemExit(77)
    source_hash = hashlib.sha256(source.read_bytes()).hexdigest()
    if source_hash != EXPECTED_SAMPLE_SHA256:
        raise SystemExit(
            "private Cmoa EPUB fingerprint mismatch: expected "
            f"{EXPECTED_SAMPLE_SHA256}, got {source_hash}"
        )

    def fixture(scratch):
        target = scratch / "cmoa-test.epub"
        shutil.copy2(source, target)
        return target, source_hash, None

    def verify(path, expected_hash, _members):
        actual = hashlib.sha256(path.read_bytes()).hexdigest()
        if actual != expected_hash:
            raise AssertionError("native Cmoa acceptance changed its private input copy")
        if hashlib.sha256(source.read_bytes()).hexdigest() != source_hash:
            raise AssertionError("native Cmoa acceptance changed the original private EPUB")

    run(
        pathlib.Path(sys.argv[1]).resolve(),
        test_entry="cmoa_epub_integration_test.cpp",
        build_fixture=fixture,
        verify=verify,
    )


if __name__ == "__main__":
    main()
