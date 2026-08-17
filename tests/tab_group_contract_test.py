#!/usr/bin/env python3

import sys
from pathlib import Path


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


repo = Path(sys.argv[1]).resolve()
tab_group_h = (repo / "src/Tabs/TabGroup.h").read_text(encoding="utf-8")
tab_group = (repo / "src/Tabs/TabGroup.cpp").read_text(encoding="utf-8")
tab_manager_h = (repo / "src/Tabs/TabManager.h").read_text(encoding="utf-8")
tab_manager = (repo / "src/Tabs/TabManager.cpp").read_text(encoding="utf-8")
cmake = (repo / "src/CMakeLists.txt").read_text(encoding="utf-8")

require(
    "class TabGroup : public QTabWidget" in tab_group_h,
    "TabGroup is the QTabWidget tab strip",
)
require(
    "int ResourceTabIndex(const Resource *resource) const;" in tab_group_h
    and "GetIdentifier()" in tab_group,
    "TabGroup looks up open tabs by Resource identifier",
)
require(
    "int AddContentTab(ContentTab *tab, bool precede_current_tab);" in tab_group_h
    and "void TakeTab(ContentTab *tab);" in tab_group_h,
    "TabGroup can insert and take a ContentTab without reloading it",
)
require(
    "class TabManager : public QWidget" in tab_manager_h
    and "class TabManager : public QTabWidget" not in tab_manager_h,
    "TabManager is no longer itself a QTabWidget",
)
require(
    "m_Primary(new TabGroup(this))" in tab_manager
    and "layout->addWidget(m_Primary, 1)" in tab_manager,
    "TabManager owns a single primary TabGroup",
)
require(
    "void OpenResource(Resource *resource," in tab_manager_h
    and "SwitchedToExistingTab" in tab_manager
    and "m_Primary->ResourceTabIndex(resource)" in tab_manager,
    "OpenResource stays on TabManager and still de-duplicates via the group index",
)
require(
    "if (!force && m_Primary->TabCount() <= 1)" in tab_manager,
    "the primary group still refuses to close its last tab unless forced",
)
require(
    "CreateTabForResource" in tab_manager
    and "m_Primary)" in tab_manager,
    "new ContentTabs are parented to the TabGroup, not to TabManager",
)
require(
    "Tabs/TabGroup.cpp" in cmake and "Tabs/TabGroup.h" in cmake,
    "TabGroup must be part of the Sigil target",
)

print("tab_group_contract: ok")
