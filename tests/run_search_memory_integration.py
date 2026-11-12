"""Verify current search fields and history across real MainWindow processes."""
import argparse
import pathlib

from run_opf_resource_integration import epub_fixture, run


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("build", type=pathlib.Path)
    args = parser.parse_args()
    phases = ("legacy", "draft", "read-draft", "read-empty", "history", "mode-switch",
              "exit-draft", "read-exit", "stale-window", "clear", "read-clear")
    run(args.build.resolve(), "search_memory_integration_test.cpp", epub_fixture,
        lambda *unused: None, direct_app_executable=True,
        run_arguments=[(mode, phase) for mode in ("original", "plus") for phase in phases])
