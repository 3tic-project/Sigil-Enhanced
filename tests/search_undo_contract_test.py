#!/usr/bin/env python3

import sys
from pathlib import Path


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


repo = Path(sys.argv[1]).resolve()
operations = (repo / "src/Misc/SearchOperations.cpp").read_text(encoding="utf-8")
coordinator = (repo / "src/MainUI/SearchBatchCoordinator.cpp").read_text(encoding="utf-8")
text_resource = (repo / "src/ResourceObjects/TextResource.cpp").read_text(encoding="utf-8")
html_resource = (repo / "src/ResourceObjects/HTMLResource.cpp").read_text(encoding="utf-8")

require(
    operations.count("SetTextAsUndoableEdit(new_text)") == 6,
    "all standard, Plus, and Python cross-resource search writes must be undoable",
)
require(
    "SetText(new_text)" not in operations,
    "search operations must not clear undo history when committing replacements",
)

commit_start = coordinator.index("QStringList appliedPaths;")
commit_end = coordinator.index("if (!result.success) {", commit_start)
commit_body = coordinator[commit_start:commit_end]
require(
    "SetTextAsUndoableEdit(result.changedTexts.value(path))" in commit_body,
    "saved-search batch commit must create one undo step per changed resource",
)
require(
    "resource->SetText(" not in commit_body,
    "successful saved-search commit must not use the undo-clearing load path",
)

require(
    "m_TextDocument->replaceTextAsSingleUndoStep(text);" in text_resource,
    "TextResource undoable writes must delegate to TextDocument",
)
require(
    "TextResource::SetTextAsUndoableEdit(text);" in html_resource
    and "emit TextChanging();" in html_resource
    and "TrackNewResources();" in html_resource,
    "HTML undoable writes must preserve tab and linked-resource notifications",
)

print("search undo wiring contract passed")
