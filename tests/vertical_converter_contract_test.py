#!/usr/bin/env python3

import re
import sys
from pathlib import Path


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


repo = Path(sys.argv[1]).resolve()
converter = (
    repo / "src/BuiltinPlugins/VerticalToHorizontalConverter.cpp"
).read_text(encoding="utf-8")
main_window = (repo / "src/MainUI/MainWindowExt.cpp").read_text(encoding="utf-8")

# The stale-source guard must compare the live OPF with the original snapshot,
# not with the transformed output. Comparing with the output aborts every real
# conversion as soon as page-progression-direction changes.
require(
    "const QString opf_source = opf->GetText();" in converter,
    "conversion must retain the original OPF snapshot",
)
require(
    "if (opf_changed && opf->GetText() != opf_source)" in converter,
    "OPF stale-source guard must compare against the original snapshot",
)
require(
    "if (opf_changed && opf->GetText() != opf_text)" not in converter,
    "OPF stale-source guard must not compare against transformed output",
)

# DPFJ/EBPAJ paired .vrtl/.hltr stylesheets are shared by both directions.
# Switching the root class is sufficient; rewriting that stylesheet globally
# makes horizontal pages inherit vertical rules (or vice versa).
require(
    re.search(
        r"options\.mode\s*==\s*VerticalCssTransformer::ConversionMode::ProfileAwareRewrite"
        r"\s*&&\s*!switch_target_class",
        converter,
    )
    is not None,
    "paired profile conversion must preserve shared .vrtl/.hltr CSS",
)

# Keep public actions and the direction enum wired in the same direction.
require(
    re.search(
        r"bool MainWindow::ConvertVerticalToHorizontal\(\)\s*\{\s*"
        r"return ConvertVerticalLayoutDirection\(true\);\s*\}",
        main_window,
    )
    is not None,
    "vertical-to-horizontal action is wired to the wrong direction",
)
require(
    re.search(
        r"bool MainWindow::ConvertHorizontalToVertical\(\)\s*\{\s*"
        r"return ConvertVerticalLayoutDirection\(false\);\s*\}",
        main_window,
    )
    is not None,
    "horizontal-to-vertical action is wired to the wrong direction",
)

print("vertical converter transaction and direction contracts verified")
