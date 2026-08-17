#!/usr/bin/env python3

import sys
from pathlib import Path


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


repo = Path(sys.argv[1]).resolve()
tab_manager_h = (repo / "src/Tabs/TabManager.h").read_text(encoding="utf-8")
tab_manager = (repo / "src/Tabs/TabManager.cpp").read_text(encoding="utf-8")
tab_bar = (repo / "src/Tabs/TabBar.cpp").read_text(encoding="utf-8")
book_browser = (repo / "src/MainUI/BookBrowser.cpp").read_text(encoding="utf-8")
main_window = (repo / "src/MainUI/MainWindow.cpp").read_text(encoding="utf-8")
settings = (repo / "src/Misc/SettingsStoreExtend.cpp").read_text(encoding="utf-8")
prefs = (repo / "src/Form_Files/PModifiedVerPrefs.ui").read_text(encoding="utf-8")

require(
    "void OpenResource(" in tab_manager_h
    and "void OpenResourceInOtherGroup(" in tab_manager_h
    and "enum class OpenDisposition" in tab_manager_h,
    "existing OpenResource slot stays; OtherGroup is a new entry",
)
require(
    "OpenDisposition::OtherGroup" in tab_manager
    and "already open in the other editor group." in tab_manager,
    "re-opening an existing resource focuses it instead of cloning",
)
require(
    'getOtherGroupTarget()' in tab_manager
    and 'target == QLatin1String("lower")' in tab_manager
    and 'target == QLatin1String("upper")' in tab_manager,
    "OtherGroup target honors inactive/lower/upper",
)
require(
    "MoveTabToOtherGroup" in tab_manager
    and "source->TakeTab(tab)" in tab_manager
    and "InsertContentTab" in tab_manager
    and "CanMoveTab" in tab_manager,
    "Move takes the same ContentTab; last primary tab cannot move",
)
require(
    "return m_Secondary;" in tab_manager.split("if (!IsSplit())")[1][:220],
    "Open/Move before a manual split must create and use the lower group",
)
require(
    'tr("Open in Other Editor Group")' in book_browser
    and "OpenInOtherEditorGroupRequest" in book_browser
    and "OpenResourceInOtherGroup" in main_window,
    "Book Browser can open a resource in the other group",
)
require(
    'tr("Move Editor to Other Group")' in tab_bar
    and "MoveToOtherGroupRequest" in tab_bar,
    "tab context menu moves the editor instead of cloning it",
)
require(
    'editorGroups/otherGroupTarget' in settings
    and "rbOtherGroupInactive" in prefs
    and "rbOtherGroupLower" in prefs
    and "rbOtherGroupUpper" in prefs,
    "Enhanced prefs expose the Other Group target setting",
)

print("open_other_group_contract: ok")
