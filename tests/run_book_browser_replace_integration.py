"""Replace an existing font through the real file picker and BookBrowser."""
import argparse
import pathlib

from run_opf_resource_integration import epub_fixture, run

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("build", type=pathlib.Path)
    parser.add_argument("--style", choices=("qt", "native", "all"), default="all")
    args = parser.parse_args()
    styles = ("qt", "native") if args.style == "all" else (args.style,)
    run(args.build.resolve(), "book_browser_replace_integration_test.cpp", epub_fixture,
        lambda *unused: None, direct_app_executable=True,
        run_arguments=[(style,) for style in styles], run_timeout=30)
