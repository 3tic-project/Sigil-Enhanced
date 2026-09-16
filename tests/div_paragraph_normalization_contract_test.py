#!/usr/bin/env python3

import sys
from pathlib import Path


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def function_body(source: str, signature: str, next_signature: str) -> str:
    start = source.index(signature)
    end = source.index(next_signature, start)
    return source[start:end]


repo = Path(sys.argv[1]).resolve()
main_ext = (repo / "src/MainUI/MainWindowExt.cpp").read_text(encoding="utf-8")
main = (repo / "src/MainUI/MainWindow.cpp").read_text(encoding="utf-8")
main_ui = (repo / "src/Form_Files/main.ui").read_text(encoding="utf-8")
options_dialog = (
    repo / "src/Dialogs/DivParagraphNormalizationDialog.cpp"
).read_text(encoding="utf-8")
preview_dialog = (
    repo / "src/Dialogs/DivParagraphNormalizationPreviewDialog.cpp"
).read_text(encoding="utf-8")
settings = (repo / "src/Misc/SettingsStoreExtend.cpp").read_text(encoding="utf-8")
stylesheet_resolver = (
    repo / "src/BuiltinPlugins/DivParagraphStylesheetResolver.cpp"
).read_text(encoding="utf-8")

for scope_text in (
    "Current XHTML file",
    "Selected XHTML files (%1)",
    "All XHTML files in the book",
):
    require(scope_text in options_dialog, f"missing scope choice: {scope_text}")

for option_name in (
    "m_BlankLines",
    "m_SceneBreaks",
    "m_ImageWrappers",
    "m_SingleBlockWrappers",
    "m_FormatSource",
):
    require(option_name in options_dialog, f"missing DIV option control: {option_name}")
require(
    "bodyParagraphs->setChecked(true);" in options_dialog
    and "bodyParagraphs->setEnabled(false);" in options_dialog,
    "safe body paragraph classification must remain mandatory",
)
require(
    "No force-all mode is provided." in options_dialog,
    "the dialog must explain that complex structures cannot be forced",
)

for preview_feature in (
    "Source Diff",
    "Before Preview",
    "After Preview",
    "CSS risk",
    "SelectedResourceIds",
):
    require(preview_feature in preview_dialog, f"missing preview feature: {preview_feature}")

run = function_body(
    main_ext,
    "bool MainWindow::RunCmoaParagraphNormalization(",
    "bool MainWindow::RunBookLiveCompatibilityAutomation()",
)
first_plan = run.index("BuildCmoaPlanWithProgress(")
preview = run.index("preview.exec()", first_plan)
selection = run.index("preview.SelectedResourceIds()", preview)
second_plan = run.index("BuildCmoaPlanWithProgress(", selection)
snapshot = run.index("SearchBatchCoordinator::CaptureSnapshot(", second_plan)
css_conflict = run.index("DivPlan::revisionConflicts(", snapshot)
commit = run.index("SearchBatchCoordinator::CommitStagedResult(", css_conflict)
require(
    first_plan < preview < selection < second_plan < snapshot < css_conflict < commit,
    "interactive flow must preview, re-plan the selected subset, recheck revisions, then commit",
)
require(
    "CommitStagedResult(\n        this, text_resources, snapshot, staged, true)" in run,
    "DIV batch commit must request a recovery checkpoint",
)
require(
    "SetText(" not in run and "SetTextAsUndoableEdit(" not in run,
    "DIV workflow must delegate writes and rollback to the shared batch coordinator",
)
require(
    "CaptureCssTexts" in run and run.count("CaptureCssTexts") >= 3,
    "CSS content must be captured for preview, replanning, and final conflict checks",
)
require(
    "QProgressDialog" in main_ext and "progress.wasCanceled()" in main_ext,
    "analysis must expose cancellable progress",
)
require(
    "DivParagraphStylesheetResolver::resolve(" in main_ext
    and "cssImports" in stylesheet_resolver
    and "addCssSource" in stylesheet_resolver
    and "xml-stylesheet" in stylesheet_resolver,
    "linked, imported, inline-imported, and XML processing-instruction CSS must be resolved",
)

compat = function_body(
    main_ext,
    "bool MainWindow::RunBookLiveCompatibilityAutomation()",
    "bool MainWindow::AnalyzeVerticalLayout()",
)
require(
    "DivOptions::bookLiveCompatibility()" in compat
    and "SearchBatchCoordinator::CommitStagedResult(" in compat,
    "legacy Automate preset must remain explicit and use atomic commit",
)
require(
    'cmd == "NormalizeBookLiveParagraphs") success = RunBookLiveCompatibilityAutomation()'
    in main,
    "legacy NormalizeBookLiveParagraphs Automate command ID must remain mapped",
)

for action_name in (
    'action name="actionAnalyzeCmoaParagraphs"',
    'action name="actionNormalizeCurrentCmoaParagraphs"',
    'action name="actionNormalizeCmoaParagraphs"',
):
    require(action_name in main_ui, f"Cmoa action is missing: {action_name}")

for action_name in (
    'action name="actionAnalyzeBookLiveParagraphs"',
    'action name="actionNormalizeCurrentBookLiveParagraphs"',
    'action name="actionNormalizeBookLiveParagraphs"',
):
    require(action_name in main_ui, f"legacy action ID was removed: {action_name}")
require(
    "Analyze BookLive Div Paragraphs (Whole Book)..." in main_ui
    and "Normalize Cmoa DIV Paragraphs..." in main_ui,
    "BookLive compatibility and Cmoa source-preserving workflows must remain separate",
)
booklive_current = function_body(
    main_ext,
    "bool MainWindow::NormalizeCurrentBookLiveParagraphs()",
    "bool MainWindow::NormalizeAllBookLiveParagraphs()",
)
require(
    "BookLiveParagraphNormalizer::normalizeXhtmlText(" in booklive_current
    and "RunCmoaParagraphNormalization" not in booklive_current,
    "the original BookLive current-file action must not route through Cmoa",
)

for key in (
    "convert_blank_lines",
    "convert_scene_breaks",
    "convert_image_wrappers",
    "convert_single_block_wrappers",
    "format_source",
):
    require(
        f'value(QStringLiteral("paragraph_normalization/{key}"), false)' in settings,
        f"optional setting must default off: {key}",
    )

print("DIV paragraph normalization UI/transaction contract passed")
