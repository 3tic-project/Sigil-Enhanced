"""Regenerate the fixed two-tag repository used by checkpoint compatibility tests.

Run with the bundled Python dependencies (dulwich 1.0.0). The committed ZIP is
the test input; this generator is only for intentional fixture updates.
"""

import os
import sys
import tempfile
import time
import zipfile
from pathlib import Path
from unittest.mock import patch


ROOT = Path(__file__).resolve().parents[2]
sys.path[:0] = [
    str(ROOT / "src/Resource_Files/python3lib"),
    str(ROOT / "src/Resource_Files/plugin_launchers/python"),
]
from repomanager import performCommit, update_annotated_tag_message


OUTPUT = Path(__file__).with_name("checkpoint_legacy_repo.zip")
BOOK_ID = "fixture-book"
BOOK_INFO = ("demo.epub", "示例书", "2026-01-02")


def write_book_file(book_root, name, contents):
    path = book_root / name
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(contents, encoding="utf-8")


def main():
    os.environ["TZ"] = "UTC"
    if hasattr(time, "tzset"):
        time.tzset()
    os.environ.pop("GIT_AUTHOR_DATE", None)
    os.environ.pop("GIT_COMMITTER_DATE", None)

    with tempfile.TemporaryDirectory() as temporary:
        base = Path(temporary)
        book_root = base / "book"
        repo_root = base / "repos"
        book_root.mkdir()
        repo_root.mkdir()
        initial = {
            "META-INF/container.xml": '<container version="1.0"/>\n',
            "META-INF/rights.xml": '<rights xmlns="urn:test"/>\n',
            "META-INF/encryption.xml": '<encryption xmlns="urn:test"/>\n',
            "OEBPS/content.opf": '<package version="3.0"/>\n',
            "OEBPS/Text/章.xhtml": '<p>一</p>\n',
        }
        for name, contents in initial.items():
            write_book_file(book_root, name, contents)

        with patch("time.time", return_value=1_750_000_000):
            performCommit(str(repo_root), BOOK_ID, BOOK_INFO, str(book_root), list(initial))

        write_book_file(book_root, "OEBPS/Text/章.xhtml", '<p>二</p>\n')
        write_book_file(book_root, "OEBPS/Text/次.xhtml", '<p>次</p>\n')
        with patch("time.time", return_value=1_750_000_060):
            performCommit(
                str(repo_root), BOOK_ID, BOOK_INFO, str(book_root),
                [*initial, "OEBPS/Text/次.xhtml"],
            )
        update_annotated_tag_message(str(repo_root), BOOK_ID, "V0001", "初始版本")

        repository = repo_root / f"epub_{BOOK_ID}"
        with zipfile.ZipFile(OUTPUT, "w") as archive:
            for path in sorted(repository.rglob("*")):
                if not path.is_file():
                    continue
                name = str(path.relative_to(repository)).replace(os.sep, "/")
                # The index and reflogs carry filesystem stat data. Dulwich
                # recreates the index on checkout; neither is needed to read
                # the fixed commits and annotated tags.
                if name == ".git/index" or name.startswith(".git/logs/"):
                    continue
                info = zipfile.ZipInfo(name, (1980, 1, 1, 0, 0, 0))
                info.compress_type = zipfile.ZIP_DEFLATED
                info.external_attr = 0o100644 << 16
                archive.writestr(info, path.read_bytes())
    print(OUTPUT)


if __name__ == "__main__":
    main()
