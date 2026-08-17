#!/usr/bin/env python3

import sys
from pathlib import Path


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


repo = Path(sys.argv[1]).resolve()
tab_manager = (repo / "src/Tabs/TabManager.cpp").read_text(encoding="utf-8")
tab_manager_h = (repo / "src/Tabs/TabManager.h").read_text(encoding="utf-8")
main_window = (repo / "src/MainUI/MainWindow.cpp").read_text(encoding="utf-8")

require(
    "void SetActiveGroup(TabGroup *group);" in tab_manager_h
    and "OnApplicationFocusChanged" in tab_manager_h
    and "focusChanged(QWidget *, QWidget *)" in tab_manager,
    "focus in either group must be able to change the active group",
)
require(
    "GetCurrentContentTab()" in tab_manager.split("void TabManager::EmitTabChanged")[1][:500]
    and "from != m_Active" in tab_manager,
    "TabChanged follows the active group, not only the primary strip",
)
require(
    "ChangeSignalsWhenTabChanges" in main_window
    and "SetActiveGroup" in tab_manager,
    "switching the active group must emit TabChanged so MainWindow rewires actions",
)
require(
    'settings.setValue(QStringLiteral("enabled"), IsSplit())' in tab_manager
    and 'settings.setValue(QStringLiteral("splitterState")' in tab_manager
    and "RestoreLayoutSettings" in tab_manager
    and "SaveLayoutSettings" in main_window
    and "RestoreLayoutSettings" in main_window,
    "restart persists split layout only, not open files",
)
require(
    'tr("Focus Upper Editor Group")' in main_window
    and 'tr("Focus Lower Editor Group")' in main_window
    and '"MainWindow.FocusUpperEditorGroup"' in main_window,
    "focus-other-group actions exist without requiring a default shortcut",
)
require(
    "m_EmptyLabel->setFocus()" in tab_manager
    and "Files are not restored. Keep the first opened HTML in the primary group." in tab_manager
    and "UpdateUIOnTabCountChange();" in main_window.split("RestoreLayoutSettings();")[1][:400],
    "empty lower group keeps focus; restart restores split only and first file stays primary",
)
require(
    "ContentTab *keep = GetCurrentContentTab();" in tab_manager
    and "QList<QPair<Resource *, bool> > items;" in tab_manager,
    "Join keeps the current tab; ReopenTabs puts each file back in its original group",
)

print("editor_group_focus_contract: ok")
