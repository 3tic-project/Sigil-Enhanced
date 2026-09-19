"""Run the native Cmoa acceptance test against a private EPUB fixture.

Set SIGIL_CMOA_TEST_EPUB to the fixture path.  A missing fixture returns 77 so
CTest reports the private acceptance test as skipped rather than passed.
"""

import hashlib
import os
import pathlib
import re
import shutil
import sys
import zipfile

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

    output_value = os.environ.get("SIGIL_CMOA_NORMALIZED_OUTPUT", "")

    def fixture(scratch):
        target = scratch / "cmoa-test.epub"
        shutil.copy2(source, target)
        os.environ["SIGIL_CMOA_NORMALIZED_OUTPUT"] = output_value or str(scratch / "normalized.epub")
        return target, source_hash, None

    def verify(path, expected_hash, _members):
        actual = hashlib.sha256(path.read_bytes()).hexdigest()
        if actual != expected_hash:
            raise AssertionError("native Cmoa acceptance changed its private input copy")
        if hashlib.sha256(source.read_bytes()).hexdigest() != source_hash:
            raise AssertionError("native Cmoa acceptance changed the original private EPUB")
        normalized = pathlib.Path(os.environ["SIGIL_CMOA_NORMALIZED_OUTPUT"])
        expected_changes = {"item/standard.opf"} | {
            f"item/xhtml/p-{number:03}.xhtml" for number in range(2, 14)
        }
        with zipfile.ZipFile(path) as original_zip, zipfile.ZipFile(normalized) as output_zip:
            original_names = set(original_zip.namelist())
            output_names = set(output_zip.namelist())
            if original_names != output_names:
                raise AssertionError("Cmoa export changed the EPUB member list")
            changed = {name for name in original_names
                       if original_zip.read(name) != output_zip.read(name)}
            if changed != expected_changes:
                raise AssertionError(f"Cmoa export changed unexpected members: {changed ^ expected_changes}")
            modified_date = rb'(<meta property="dcterms:modified">)[^<]*(</meta>)'
            original_opf = re.sub(modified_date, rb'\1DATE\2',
                                  original_zip.read("item/standard.opf"))
            output_opf = re.sub(modified_date, rb'\1DATE\2',
                                output_zip.read("item/standard.opf"))
            if original_opf != output_opf:
                raise AssertionError("Cmoa export changed unrelated OPF bytes")
        print("Cmoa export changed only 12 planned XHTML members and the OPF modified date")

    run(
        pathlib.Path(sys.argv[1]).resolve(),
        test_entry="cmoa_epub_integration_test.cpp",
        build_fixture=fixture,
        verify=verify,
    )


if __name__ == "__main__":
    main()
