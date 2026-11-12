"""Exercise window and panel persistence across real MainWindow processes."""

import argparse
import json
import pathlib

from run_opf_resource_integration import epub_fixture, run


def layout_fixture(scratch):
    (scratch / "offscreen.json").write_text(json.dumps({
        "screens": [{"name": "WindowLayoutTest", "width": 1800, "height": 1200}]
    }), encoding="utf-8")
    return epub_fixture(scratch)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("build", type=pathlib.Path)
    args = parser.parse_args()
    run(
        args.build.resolve(),
        "main_window_layout_integration_test.cpp",
        layout_fixture,
        lambda *unused: None,
        direct_app_executable=True,
        run_arguments=[(scenario,) for scenario in (
            "fresh", "write-normal", "read-normal", "legacy", "write-maximized", "read-maximized",
            "write-fullscreen", "read-fullscreen", "write-minimized", "read-normal",
            "disabled", "reenabled", "corrupt", "offscreen",
        )],
    )
