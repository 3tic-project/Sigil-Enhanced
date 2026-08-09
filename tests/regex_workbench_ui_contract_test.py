#!/usr/bin/env python3

import sys
import xml.etree.ElementTree as ET
from pathlib import Path


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


repo = Path(sys.argv[1]).resolve()
main_ui_path = repo / "src/Form_Files/main.ui"
main_ui = ET.parse(main_ui_path).getroot()
main_window = (repo / "src/MainUI/MainWindow.cpp").read_text(encoding="utf-8")
main_window_ext = (repo / "src/MainUI/MainWindowExt.cpp").read_text(encoding="utf-8")
main_window_header = (repo / "src/MainUI/MainWindow.h").read_text(encoding="utf-8")
dialog = (repo / "src/Dialogs/RegexWorkbenchDialog.cpp").read_text(encoding="utf-8")
automate_editor = (repo / "src/Dialogs/AutomateEditor.cpp").read_text(encoding="utf-8")

enhancement_menu = main_ui.find(".//widget[@name='menuEnhancement']")
require(enhancement_menu is not None, "Enhancement menu must exist")
enhancement_actions = [item.get("name") for item in enhancement_menu.findall("addaction")]
require(
    enhancement_actions.count("actionOpenRegexWorkbench") == 1,
    "Regex Workbench must have exactly one Enhancement menu entry",
)
workbench_action = main_ui.find(".//action[@name='actionOpenRegexWorkbench']")
require(workbench_action is not None, "Regex Workbench QAction must exist")
require(
    workbench_action.findtext("./property[@name='text']/string")
    == "Advanced Regex Workbench...",
    "Regex Workbench QAction text changed unexpectedly",
)

require(
    'QStringLiteral("enhanced/regex_workbench_enabled"),\n                           true'
    in main_window,
    "Regex Workbench must be enabled by default but retain the settings flag",
)
require(
    "MainWindow.OpenRegexWorkbench" in main_window
    and "SLOT(OpenRegexWorkbench())" in main_window,
    "Regex Workbench action must be registered and connected",
)
require(
    "bool OpenRegexWorkbench();" in main_window_header
    and "bool RunRegexWorkbenchRecipe(const QString& identifier);" in main_window_header,
    "interactive and Automate MainWindow entry points must be declared",
)

for object_name in (
    "regexRecipeName",
    "regexRuleList",
    "regexRuleName",
    "regexSecondaryPattern",
    "regexFindPattern",
    "regexReplacePattern",
    "regexDryRunButton",
    "regexApplyButton",
    "regexReportTable",
):
    require(object_name in dialog, f"missing stable UI object name: {object_name}")
require("QtConcurrent::run" in dialog, "staging must execute outside the GUI thread")
require("processEvents" not in dialog, "dialog must not use nested processEvents loops")
require("WA_DeleteOnClose" not in dialog, "stack-owned modal dialog must not self-delete")
require(
    "m_CancelFlag->store(true" in dialog and "options.isCancelled" in dialog,
    "Cancel must reach the bounded batch engine",
)
start_run = dialog.index("void RegexWorkbenchDialog::StartRun")
snapshot = dialog.index("SearchBatchCoordinator::CaptureSnapshot(", start_run)
worker = dialog.index("m_Watcher->setFuture(QtConcurrent::run(", snapshot)
require(snapshot < worker, "each run must capture a fresh snapshot before staging")
apply_slot = dialog.index("void RegexWorkbenchDialog::StartApply")
require(
    "StartRun(RunMode::Apply);" in dialog[apply_slot:start_run],
    "Apply must restage instead of committing a cached Dry-Run result",
)
finished = dialog.index("void RegexWorkbenchDialog::RunFinished")
commit = dialog.index("RegexWorkbenchBatchCommitter::Commit(", finished)
require(finished < commit, "Apply must commit only after worker staging finishes")

open_start = main_window_ext.index("bool MainWindow::OpenRegexWorkbench()")
open_end = main_window_ext.index("bool MainWindow::RunRegexWorkbenchRecipe", open_start)
open_body = main_window_ext[open_start:open_end]
require(
    "RegexWorkbenchDialog dialog(this, targets, this);" in open_body
    and "dialog.exec();" in open_body,
    "interactive entry must use a stack-owned modal dialog",
)
automation_start = main_window_ext.index("bool MainWindow::RunRegexWorkbenchRecipe")
automation_body = main_window_ext[automation_start:]
load = automation_body.index("RegexRecipeStore::LoadNamed")
capture = automation_body.index("SearchBatchCoordinator::CaptureSnapshot", load)
stage = automation_body.index("RegexWorkbenchBatchRunner::Run", capture)
commit = automation_body.index("RegexWorkbenchBatchCommitter::Commit", stage)
require(
    load < capture < stage < commit,
    "Automate recipes must load, snapshot, stage in memory, then commit",
)
require(
    "targets.allTextPaths" in automation_body,
    "Automate recipes must have a deterministic all-text target scope",
)

require(
    main_window.count('"RunRegexWorkbenchRecipe"') >= 2
    and 'cmd.startsWith("RunRegexWorkbenchRecipe ")' in main_window,
    "Automate dispatcher must advertise and parse the parameterized recipe command",
)
require(
    automate_editor.count('"RunRegexWorkbenchRecipe"') >= 3
    and "[Regex recipe name or absolute path here]" in automate_editor,
    "Automate editor must expose and persist the recipe parameter",
)

print("regex workbench UI and automation contract passed")
