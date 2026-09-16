"""Inject live host commit failures and verify compensation on real Book objects."""
import argparse
import pathlib

from run_opf_resource_integration import epub_fixture, run


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('build', type=pathlib.Path)
    run(parser.parse_args().build.resolve(), 'plugin_session_fault_recovery_integration_test.cpp',
        epub_fixture, lambda *args: None, direct_app_executable=True)
