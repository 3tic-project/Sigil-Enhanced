#!/usr/bin/env python3

import sys
from pathlib import Path


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


repo = Path(sys.argv[1]).resolve()
operations = (repo / "src/Misc/SearchOperations.cpp").read_text(encoding="utf-8")
coordinator = (repo / "src/MainUI/SearchBatchCoordinator.cpp").read_text(encoding="utf-8")
coordinator_header = (repo / "src/MainUI/SearchBatchCoordinator.h").read_text(encoding="utf-8")
workbench_committer = (
    repo / "src/MainUI/RegexWorkbenchBatchCommitter.cpp"
).read_text(encoding="utf-8")
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
    "CaptureSnapshot" in coordinator_header
    and "CommitStagedResult" in coordinator_header,
    "search batch coordinator must expose separate GUI snapshot and commit boundaries",
)
run_start = coordinator.index("SearchBatch::Result SearchBatchCoordinator::Run(")
capture_call = coordinator.index("CaptureSnapshot(", run_start)
stage_call = coordinator.index("SearchBatch::Runner::Run(", capture_call)
commit_call = coordinator.index("CommitStagedResult(", stage_call)
require(
    capture_call < stage_call < commit_call,
    "saved-search compatibility wrapper must snapshot, stage in memory, then commit",
)
checkpoint_call = coordinator.index("CreateRecoveryCheckpoint()", commit_call)
undoable_write = coordinator.index(
    "SetTextAsUndoableEdit(result.changedTexts.value(path))", checkpoint_call
)
require(
    checkpoint_call < undoable_write,
    "recovery checkpoint must succeed before the first staged resource write",
)
workbench_commit_call = workbench_committer.index(
    "SearchBatchCoordinator::CommitStagedResult("
)
capture_only_conflict_check = workbench_committer.index(
    "SearchBatchCoordinator::ResourcesMatchSnapshot("
)
store_publish = workbench_committer.index("store = pendingStore;", workbench_commit_call)
require(
    "pendingStore.restore(batch_result.finalStore" in workbench_committer
    and "result.changedTexts.isEmpty()" in workbench_committer
    and capture_only_conflict_check < workbench_commit_call < store_publish,
    "workbench variables must validate before commit and publish only after document commit",
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
