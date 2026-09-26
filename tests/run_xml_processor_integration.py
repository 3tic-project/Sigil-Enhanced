"""Compare the native XML repair and reference-update callers with the embedded Python xmlprocessor."""
import argparse
import pathlib

from run_opf_resource_integration import run


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('build', type=pathlib.Path)
    run(parser.parse_args().build.resolve(), 'xml_processor_integration_test.cpp',
        verify=lambda *args: None, run_timeout=120)
