"""Exercise Clip shortcut badges through the real Sigil main window."""
import argparse
import pathlib

from run_opf_resource_integration import epub_fixture, run


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('build', type=pathlib.Path)
    run(parser.parse_args().build.resolve(), 'clip_shortcut_badge_integration_test.cpp',
        epub_fixture, lambda *args: None, direct_app_executable=True)
