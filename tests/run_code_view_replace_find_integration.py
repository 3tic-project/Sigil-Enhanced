"""Exercise original and Plus Replace/Find through the real CodeViewEditor."""
import argparse
import pathlib

from run_opf_resource_integration import epub_fixture, run


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('build', type=pathlib.Path)
    run(parser.parse_args().build.resolve(), 'code_view_replace_find_integration_test.cpp',
        epub_fixture, lambda *args: None, direct_app_executable=True)
