"""Check native Agent edits and background refreshes against other windows/processes."""
import argparse
import pathlib
from run_opf_resource_integration import epub_fixture, run

if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('build', type=pathlib.Path)
    run(parser.parse_args().build.resolve(), 'agent_window_isolation_integration_test.cpp',
        epub_fixture, lambda *args: None, direct_app_executable=True, run_timeout=60)
