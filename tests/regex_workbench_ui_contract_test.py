#!/usr/bin/env python3

import re
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
search_editor_model = (
    repo / "src/MiscEditors/SearchEditorModelPlus.cpp"
).read_text(encoding="utf-8")
user_guide = (repo / "docs/AdvancedRegexWorkbench.md").read_text(encoding="utf-8")

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
    "regexCaptureOnly",
    "regexImportSearchTemplate",
    "regexEditingSplitter",
    "regexDryRunButton",
    "regexApplyButton",
    "regexReportTable",
    "regexVariablesPanel",
    "regexVariableTable",
):
    require(object_name in dialog, f"missing stable UI object name: {object_name}")
require("QtConcurrent::run" in dialog, "staging must execute outside the GUI thread")
require(
    "m_ImportButton->setVisible(false);" in dialog,
    "Search Template import must remain hidden while compatibility is under review",
)
require(
    "CorrectLegacyJapaneseQuoteSpacing(" in search_editor_model,
    "persisted legacy Japanese quote templates must be migrated when loaded",
)
require(
    "new QSplitter(Qt::Horizontal" in dialog
    and "editingSplitter->setStretchFactor(1, 6);" in dialog
    and "editingSplitter->setStretchFactor(2, 3);" in dialog
    and "new QGroupBox(tr(\"Patterns\")" in dialog
    and "new QGroupBox(tr(\"Options\")" in dialog
    and "new QGroupBox(tr(\"Named captures\")" in dialog
    and "new QGroupBox(tr(\"Variables\"), runBox)" in dialog
    and "new QTableWidget(variablesBox)" in dialog,
    "rule editing controls must use the resizable grouped layout",
)
require(
    "m_ApplyButton->setDefault(true);" in dialog,
    "Apply must use the dialog's prominent confirmation-button styling",
)
require(
    "bool RegexWorkbenchDialog::IsPristineNewRecipe()" in dialog
    and "Creating a new recipe will clear the current rules" in dialog
    and "QMessageBox::Yes | QMessageBox::Cancel" in dialog,
    "New must warn before clearing a non-pristine recipe",
)
collect_start = main_window_ext.index(
    "RegexWorkbenchDialog::TargetSet CollectRegexWorkbenchTargets"
)
collect_end = main_window_ext.index("QHash<Resource*, QString>", collect_start)
collect_body = main_window_ext[collect_start:collect_end]
require(
    "targets.specialPaths.append(path);" in collect_body
    and "targets.selectedPaths.sort();" in collect_body
    and "targets.specialPaths.sort();" in collect_body
    and "targets.allTextPaths.sort();" in collect_body
    and 'tr("All special text files (%1)")' in dialog
    and "case TargetScope::AllSpecial:" in dialog,
    "OPF, NCX, and other special text resources must have a stable target scope",
)
require("processEvents" not in dialog, "dialog must not use nested processEvents loops")
require("WA_DeleteOnClose" not in dialog, "stack-owned modal dialog must not self-delete")
require(
    "m_CancelFlag->store(true" in dialog and "options.isCancelled" in dialog,
    "Cancel must reach the bounded batch engine",
)
require(
    "exactSnapshotNavigationAvailable" in dialog
    and "OpenFileRequest(bookpath," in dialog
    and "OpenFileAndSelect" in main_window_ext
    and "tab->SetSelectionRange(start, end)" in main_window,
    "Dry-Run result activation must open its snapshot location and highlight exact ranges",
)

error_sources = (
    "src/BuiltinPlugins/RegexWorkbench/RecursiveReplaceGuard.cpp",
    "src/BuiltinPlugins/RegexWorkbench/RegexRecipeSearchEditorAdapter.cpp",
    "src/BuiltinPlugins/RegexWorkbench/RegexRecipeStore.cpp",
    "src/BuiltinPlugins/RegexWorkbench/RegexWorkbenchBatchRunner.cpp",
    "src/BuiltinPlugins/RegexWorkbench/RegexWorkbenchEngine.cpp",
    "src/BuiltinPlugins/RegexWorkbench/RegexWorkbenchVariableExecutor.cpp",
    "src/BuiltinPlugins/RegexWorkbench/SearchVariableStore.cpp",
    "src/BuiltinPlugins/RegexWorkbench/SecondaryRegexMatcher.cpp",
    "src/MainUI/RegexWorkbenchBatchCommitter.cpp",
    "src/MainUI/SearchBatchCoordinator.cpp",
    "src/Misc/RegexMatchEnumerator.cpp",
    "src/Misc/SearchBatchRunner.cpp",
    "src/Misc/StagedTextValidator.cpp",
)
raw_error_literal = re.compile(
    r'(?:error(?:Message)?\s*=|SetError\(|Fail\(|Failure\()'
    r'[\s\S]{0,180}?QStringLiteral\("[A-Za-z]'
)
for relative_path in error_sources:
    source = (repo / relative_path).read_text(encoding="utf-8")
    require(
        "RegexWorkbenchCore" in source,
        f"workbench error source lacks a translation context: {relative_path}",
    )
    require(
        raw_error_literal.search(source) is None,
        f"workbench error source contains an untranslatable literal: {relative_path}",
    )
start_run = dialog.index("void RegexWorkbenchDialog::StartRun")
snapshot = dialog.index("SearchBatchCoordinator::CaptureSnapshot(", start_run)
worker = dialog.index("m_Watcher->setFuture(QtConcurrent::run(", snapshot)
confirmation = dialog.index("QMessageBox::question(", start_run)
require(
    confirmation < snapshot,
    "Apply confirmation must happen before the fresh document snapshot",
)
require(snapshot < worker, "each run must capture a fresh snapshot before staging")
apply_slot = dialog.index("void RegexWorkbenchDialog::StartApply")
require(
    "StartRun(RunMode::Apply);" in dialog[apply_slot:start_run],
    "Apply must restage instead of committing a cached Dry-Run result",
)
finished = dialog.index("void RegexWorkbenchDialog::RunFinished")
commit = dialog.index("RegexWorkbenchBatchCommitter::Commit(", finished)
require(finished < commit, "Apply must commit only after worker staging finishes")
require(
    dialog.index("Creating the recovery checkpoint and committing staged changes", finished)
    < commit,
    "the UI must announce the checkpoint/commit boundary before writing",
)

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
    "m_LastRecipePath" in dialog
    and "settings.setValue(LastRecipeKey, m_LastRecipePath)" in dialog,
    "the last recipe location must survive creating a new unsaved recipe",
)
require(
    "rule.secondaryMode == SecondaryMode::None\n                                ? QString()"
    in dialog,
    "switching secondary mode to None must not save a hidden stale pattern",
)
require(
    "m_AllowEmpty->setEnabled(recursive);" in dialog
    and "m_AllowEmpty->setChecked(false);" in dialog,
    "zero-length matching controls must follow the recursive recipe invariant",
)
require(
    "rule.captureOnly = m_CaptureOnly->isChecked();" in dialog
    and "m_ReplacePattern->setEnabled(!captureOnly && !m_Busy);" in dialog
    and "m_Recursive->setEnabled(!captureOnly && !m_Busy);" in dialog
    and "m_VariableExpansion->setEnabled(!captureOnly && !m_Busy);" in dialog,
    "capture-only rules must disable all replacement-producing controls",
)
require(
    "m_LastResult.report.totalMatches" in dialog
    and "m_LastResult.report.totalReplacements" in dialog,
    "workbench completion status must distinguish matches from replacements",
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
for documented_contract in (
    "Enhancement > Advanced Regex Workbench...",
    "${var:name}",
    "Capture variables only",
    "RunRegexWorkbenchRecipe",
    "Dry Run",
    "Checkpoint",
    "Undo",
):
    require(
        documented_contract in user_guide,
        f"user guide is missing contract: {documented_contract}",
    )

print("regex workbench UI and automation contract passed")
