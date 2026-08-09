#!/usr/bin/env python3

import sys
import xml.etree.ElementTree as ET
from pathlib import Path


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


repo = Path(sys.argv[1]).resolve()
main_ui = ET.parse(repo / "src/Form_Files/main.ui").getroot()
main_window = (repo / "src/MainUI/MainWindow.cpp").read_text(encoding="utf-8")

expected = (
    (
        "actionAnalyzeBrParagraphs",
        "Analyze Kobo BR Paragraphs (Whole Book)...",
        "AnalyzeBrParagraphs",
        "whole book",
    ),
    (
        "actionNormalizeCurrentBrParagraphs",
        "Normalize Kobo BR Paragraphs (Current File)...",
        "NormalizeCurrentBrParagraphs",
        "current XHTML file",
    ),
    (
        "actionNormalizeBrParagraphs",
        "Normalize Kobo BR Paragraphs (Whole Book)...",
        "NormalizeAllBrParagraphs",
        "whole book",
    ),
    (
        "actionAnalyzeKfxParagraphs",
        "Analyze Kindle KFX Paragraphs (Whole Book)...",
        "AnalyzeKfxParagraphs",
        "whole book",
    ),
    (
        "actionNormalizeCurrentKfxParagraphs",
        "Normalize Kindle KFX Paragraphs (Current File)...",
        "NormalizeCurrentKfxParagraphs",
        "current XHTML file",
    ),
    (
        "actionNormalizeKfxParagraphs",
        "Normalize Kindle KFX Paragraphs (Whole Book)...",
        "NormalizeAllKfxParagraphs",
        "whole book",
    ),
    (
        "actionAnalyzeBookLiveParagraphs",
        "Analyze BookLive Div Paragraphs (Whole Book)...",
        "AnalyzeBookLiveParagraphs",
        "whole book",
    ),
    (
        "actionNormalizeCurrentBookLiveParagraphs",
        "Normalize BookLive Div Paragraphs (Current File)...",
        "NormalizeCurrentBookLiveParagraphs",
        "current XHTML file",
    ),
    (
        "actionNormalizeBookLiveParagraphs",
        "Normalize BookLive Div Paragraphs (Whole Book)...",
        "NormalizeAllBookLiveParagraphs",
        "whole book",
    ),
)

enhancement_menu = main_ui.find(".//widget[@name='menuEnhancement']")
require(enhancement_menu is not None, "Enhancement menu must exist")
menu_actions = [item.get("name") for item in enhancement_menu.findall("addaction")]
expected_names = [item[0] for item in expected]
positions = [menu_actions.index(name) for name in expected_names]
require(
    positions == list(range(positions[0], positions[0] + len(expected))),
    "paragraph normalizer menu actions must remain grouped in scope order",
)

for action_name, label, slot, scope_hint in expected:
    action = main_ui.find(f".//action[@name='{action_name}']")
    require(action is not None, f"missing QAction: {action_name}")
    require(
        action.findtext("./property[@name='text']/string") == label,
        f"unexpected menu label for {action_name}",
    )
    tooltip = action.findtext("./property[@name='toolTip']/string") or ""
    require(scope_hint in tooltip, f"tooltip does not identify scope: {action_name}")
    connection = (
        f"connect(ui.{action_name}, SIGNAL(triggered()), this, SLOT({slot}()));"
    )
    require(connection in main_window, f"wrong scope handler for {action_name}")
