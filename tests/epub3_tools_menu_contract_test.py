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
main_window_h = (repo / "src/MainUI/MainWindow.h").read_text(encoding="utf-8")

menu = main_ui.find(".//widget[@name='menuEPUB3Tools']")
require(menu is not None, "Epub3 Tools menu must exist")
menu_actions = [item.get("name") for item in menu.findall("addaction")]

epub3_only = [
    "actionUpdateManifestProperties",
    "actionNCXGuideFromNav",
    "actionRemoveNCXGuide",
    "actionRemoveNavFromSpine",
    "actionAddNavToSpine",
    "actionAddNavToSpineNonLinear",
    "actionEpub3To2",
]
for name in epub3_only:
    require(name in menu_actions, f"{name} must live in Epub3 Tools")
require("actionEpub2To3" in menu_actions, "Epub2 to Epub3 must live in Epub3 Tools")

require(
    "void UpdateEpub3ToolsEnabled(const QString &epubversion);" in main_window_h,
    "MainWindow must declare UpdateEpub3ToolsEnabled",
)
require(
    "void MainWindow::UpdateEpub3ToolsEnabled(const QString &epubversion)" in main_window,
    "version-specific Epub3 Tools enablement must be centralized",
)
require(
    "ui.menuEPUB3Tools->setEnabled(true)" in main_window,
    "Epub3 Tools menu must stay enabled for EPUB2",
)
require(
    'ui.menuEPUB3Tools->setEnabled(epubversion.startsWith("3"))' not in main_window,
    "Epub3 Tools must not disable the whole menu on EPUB2",
)
require(
    "ui.actionEpub2To3->setEnabled(is_epub2)" in main_window,
    "Epub2 to Epub3 must remain enabled on EPUB2",
)
for name in epub3_only:
    require(
        f"ui.{name}->setEnabled(is_epub3)" in main_window,
        f"{name} must stay EPUB3-only",
    )
require(
    main_window.count("UpdateEpub3ToolsEnabled(epubversion);") >= 2,
    "file open and 2<->3 conversion must refresh Epub3 Tools enablement",
)

print("epub3_tools_menu_contract_test: ok")
