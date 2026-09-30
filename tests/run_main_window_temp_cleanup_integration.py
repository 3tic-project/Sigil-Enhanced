"""Verify that closing an EPUB removes its extracted working directory.

Covers the exit path, plugin-made read-only folders, files that stay held
until after the window closes (exit waits in a dialog), leftovers removed on
the next start, and Python holds on the directory.
"""

import argparse
import pathlib

from run_opf_resource_integration import epub_fixture, run


SCENARIOS = ['exit-large', 'readonly', 'held', 'give-up', 'python']


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("build", type=pathlib.Path)
    args = parser.parse_args()
    run(
        args.build.resolve(),
        "main_window_temp_cleanup_integration_test.cpp",
        epub_fixture,
        lambda *unused: None,
        direct_app_executable=True,
        run_arguments=[(scenario,) for scenario in SCENARIOS],
        run_timeout=60,
    )
