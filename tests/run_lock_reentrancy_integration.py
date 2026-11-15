"""Exercise resource, interpreter and spellcheck locks with real Sigil objects."""
import argparse
import pathlib

from run_opf_resource_integration import epub_fixture, run
from run_main_window_noop_integration import epub2_fixture

SCENARIOS = ("python-function", "python-method", "python-worker", "python-objects", "toc-switch", "toc-ncx",
             "opf-warning", "spell-reload", "heading-dialog")


def fixtures(scratch):
    epub2_fixture(scratch)
    return epub_fixture(scratch)

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("build", type=pathlib.Path)
    parser.add_argument("--scenario", choices=(*SCENARIOS, "all"), default="all")
    parser.add_argument("--timeout", type=int, default=25)
    args = parser.parse_args()
    scenarios = SCENARIOS if args.scenario == "all" else (args.scenario,)
    run(args.build.resolve(), "lock_reentrancy_integration_test.cpp", fixtures,
        lambda *_: None, direct_app_executable=True,
        run_arguments=[(scenario,) for scenario in scenarios], run_timeout=args.timeout)
