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
opf_header = (repo / "src/ResourceObjects/OPFResource.h").read_text(encoding="utf-8")
opf_resource = (repo / "src/ResourceObjects/OPFResource.cpp").read_text(encoding="utf-8")

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

# Every changed text resource must receive one whole-document undo command.
# SetText() rebuilds the QTextDocument and clears the existing undo stack.
require(
    "page.resource->SetTextAsUndoableEdit(page.transformed);" in converter,
    "converted XHTML pages must be written as undoable edits",
)
require(
    "change.first->SetTextAsUndoableEdit(change.second);" in converter,
    "converted CSS resources must be written as undoable edits",
)
require(
    "opf->SetTextAsUndoableEdit(opf_text);" in converter,
    "OPF page-progression changes must be written as undoable edits",
)
for destructive_write in (
    "page.resource->SetText(page.transformed);",
    "change.first->SetText(change.second);",
    "opf->SetText(opf_text);",
):
    require(
        destructive_write not in converter,
        "layout conversion must not clear undo history: " + destructive_write,
    )

# Recovery checkpoints snapshot the live in-memory resources without saving or
# normalising them first, so pre-existing undo commands survive batch setup.
conversion_start = main_window.index(
    "bool MainWindow::ConvertVerticalLayoutDirection(bool to_horizontal)"
)
conversion_end = main_window.index("//modified: insertFileToEditor", conversion_start)
conversion_body = main_window[conversion_start:conversion_end]
require(
    "CreateRecoveryCheckpoint()" in conversion_body,
    "layout conversion must create an isolated recovery checkpoint",
)
require(
    "RepoCommit()" not in conversion_body,
    "layout conversion must not use the undo-clearing normal checkpoint path",
)

# OPF has additional validation and notifications that the base TextResource
# implementation does not provide.
require(
    "virtual void SetTextAsUndoableEdit(const QString &text);" in opf_header,
    "OPFResource must override undoable whole-document writes",
)
opf_undo_start = opf_resource.index("void OPFResource::SetTextAsUndoableEdit")
opf_undo_end = opf_resource.index("bool OPFResource::LoadFromDisk", opf_undo_start)
opf_undo_body = opf_resource[opf_undo_start:opf_undo_end]
require(
    "emit TextChanging();" in opf_undo_body
    and "ValidatePackageVersion(text)" in opf_undo_body
    and "TextResource::SetTextAsUndoableEdit(source);" in opf_undo_body,
    "undoable OPF writes must preserve validation and editor notifications",
)
require(
    "QWriteLocker" not in opf_undo_body,
    "OPF undoable writes must not relock a caller-held non-recursive lock",
)

print("vertical converter transaction and direction contracts verified")
