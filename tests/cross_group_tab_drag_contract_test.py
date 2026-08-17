#!/usr/bin/env python3

import sys
from pathlib import Path


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


repo = Path(sys.argv[1]).resolve()
tab_bar = (repo / "src/Tabs/TabBar.cpp").read_text(encoding="utf-8")
tab_bar_h = (repo / "src/Tabs/TabBar.h").read_text(encoding="utf-8")
tab_group = (repo / "src/Tabs/TabGroup.cpp").read_text(encoding="utf-8")
tab_manager = (repo / "src/Tabs/TabManager.cpp").read_text(encoding="utf-8")

require(
    "application/x-sigil-editortab" in tab_bar
    and "void TabDropRequest(QWidget *tab, int insert_index);" in tab_bar_h
    and "drag->exec(Qt::MoveAction)" in tab_bar,
    "tabs start a MOVE drag instead of cloning the editor",
)
require(
    "setMovable(false)" in tab_group
    and "setAcceptDrops(true)" in tab_group
    and "InsertContentTab" in tab_group,
    "TabGroup accepts a dropped ContentTab and inserts the same widget",
)
require(
    "bool TabManager::MoveTabToGroup" in tab_manager
    and "source->TakeTab(tab)" in tab_manager
    and "dest->InsertContentTab(tab, dest_index)" in tab_manager
    and "CanMoveTab" in tab_manager,
    "cross-group drop is a move; the last primary tab cannot leave",
)
require(
    "AcceptsEditorTabDrop" in tab_manager
    and "m_EmptyLabel->setAcceptDrops(true)" in tab_manager,
    "an empty lower group is a drop target",
)

print("cross_group_tab_drag_contract: ok")
