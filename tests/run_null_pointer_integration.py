"""Exercise wrong selections, closed editors and destroyed resource lifetimes."""
import argparse
import pathlib

from run_opf_resource_integration import epub_fixture, run

SCENARIOS = ("copy-html", "copy-css", "font-actions", "search-original", "search-plus",
             "loading-close", "conversion-options", "conversion-preview",
             "conversion-file-apply", "conversion-svg-apply", "conversion-selection-apply",
             "resource-xhtml-pending", "resource-css-pending",
             "resource-xhtml-loaded", "resource-css-loaded",
             "resource-xhtml-direct", "resource-css-direct")

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("build", type=pathlib.Path)
    parser.add_argument("--scenario", choices=(*SCENARIOS, "all"), default="all")
    parser.add_argument("--timeout", type=int, default=25)
    args = parser.parse_args()
    scenarios = SCENARIOS if args.scenario == "all" else (args.scenario,)
    run(args.build.resolve(), "null_pointer_integration_test.cpp", epub_fixture,
        lambda *_: None, direct_app_executable=True,
        run_arguments=[(scenario,) for scenario in scenarios], run_timeout=args.timeout)
