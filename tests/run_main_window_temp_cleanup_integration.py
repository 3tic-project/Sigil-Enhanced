"""Verify that closing an EPUB removes its extracted working directory.

Covers the exit path, plugin-made read-only folders, files that stay held
until after the window closes (exit waits in a dialog), leftovers removed on
the next start, and Python holds on the directory.
"""

import argparse
import pathlib
import zipfile

from run_opf_resource_integration import epub_fixture, run


SCENARIOS = ('exit-large', 'readonly', 'held', 'give-up', 'python', 'two-books',
             'open-warning', 'open-error')


def fixture(scratch):
    path, source, members = epub_fixture(scratch)
    warning_members = dict(members)
    warning_members['OEBPS/content.opf'] = members['OEBPS/content.opf'].decode('utf-16').replace(
        " properties='nav'", '').encode('utf-16')
    with zipfile.ZipFile(str(path) + '.warning.epub', 'w') as archive:
        for name, data in warning_members.items():
            archive.writestr(name, data)
    (scratch / 'invalid.epub').write_bytes(b'Not a ZIP archive')
    return path, source, members


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("build", type=pathlib.Path)
    parser.add_argument("--scenario", choices=(*SCENARIOS, 'all'), default='all')
    args = parser.parse_args()
    run(
        args.build.resolve(),
        "main_window_temp_cleanup_integration_test.cpp",
        fixture,
        lambda *unused: None,
        direct_app_executable=True,
        run_arguments=[(scenario,) for scenario in (SCENARIOS if args.scenario == 'all' else (args.scenario,))],
        run_timeout=60,
    )
