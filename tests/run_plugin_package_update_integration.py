"""Run package source planners through the real embedded Python bridge."""
import argparse
import pathlib

from run_opf_resource_integration import run


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('build', type=pathlib.Path)
    run(parser.parse_args().build.resolve(), 'plugin_package_update_test.cpp',
        verify=lambda *args: None)
