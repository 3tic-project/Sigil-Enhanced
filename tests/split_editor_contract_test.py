#!/usr/bin/env python3

import sys
from pathlib import Path


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


repo = Path(sys.argv[1]).resolve()
tab_manager_h = (repo / "src/Tabs/TabManager.h").read_text(encoding="utf-8")
tab_manager = (repo / "src/Tabs/TabManager.cpp").read_text(encoding="utf-8")
main_window = (repo / "src/MainUI/MainWindow.cpp").read_text(encoding="utf-8")

require(
    "void SplitEditorDown();" in tab_manager_h
    and "void JoinEditorGroups();" in tab_manager_h
    and "bool IsSplit() const;" in tab_manager_h,
    "TabManager exposes split/join without changing OpenResource callers",
)
require(
    "QSplitter(Qt::Vertical" in tab_manager
    and "editorGroupSplitter" in tab_manager
    and "setChildrenCollapsible(false)" in tab_manager,
    "editor groups use a non-collapsible vertical splitter",
)
require(
    'tr("Open a file from Book Browser, or drop an editor tab here")' in tab_manager
    and 'tr("Close This Editor Group")' in tab_manager
    and "CloseEmptySecondary" in tab_manager
    and "OnEmptyGroupContextMenu" in tab_manager
    and "QStackedLayout" in tab_manager,
    "an empty secondary group can be closed from a button or the area context menu",
)
require(
    "m_Secondary->TakeTab(tab)" in tab_manager
    and "m_Primary->AddContentTab(tab, false)" in tab_manager,
    "Join moves the existing ContentTab widgets; it does not reload",
)
require(
    "const bool last_primary = (group == m_Primary && group->TabCount() <= 1)" in tab_manager,
    "only the primary group refuses to close its last tab",
)
require(
    "tabs.append(m_Secondary->Tabs())" in tab_manager
    and "FindTab(resource)" in tab_manager,
    "open-tab queries are the union of both groups",
)
require(
    'tr("Split Editor Down")' in main_window
    and 'tr("Join Editor Groups")' in main_window
    and 'tr("Editor Layout")' in main_window
    and '"MainWindow.SplitEditorDown"' in main_window
    and '"MainWindow.JoinEditorGroups"' in main_window,
    "View > Editor Layout registers split/join with no default shortcut",
)
require(
    "actionSplitSection" in main_window
    and "SplitEditorDown" in main_window,
    "Split Editor Down must stay distinct from Split At Cursor",
)

print("split_editor_contract: ok")
