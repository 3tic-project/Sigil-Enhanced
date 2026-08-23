#!/usr/bin/env python3

import sys
from pathlib import Path


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


repo = Path(sys.argv[1]).resolve()
main_window = (repo / "src/MainUI/MainWindow.cpp").read_text(encoding="utf-8")
main_window_h = (repo / "src/MainUI/MainWindow.h").read_text(encoding="utf-8")
dock_h = (repo / "src/Agent/UI/AgentDock.h").read_text(encoding="utf-8")

require(
    "class AgentDock : public QDockWidget" in dock_h,
    "AgentDock must be a QDockWidget like Book Browser / Preview",
)
require(
    "m_AgentDock" in main_window_h and "AgentDock" in main_window_h,
    "MainWindow must own the Agent dock",
)
require(
    "addDockWidget" in main_window and "m_AgentDock" in main_window,
    "Agent dock must be added to the main window, not opened as a plugin dialog",
)
require(
    "Plugins/" not in dock_h and "PluginRunner" not in dock_h,
    "AgentDock must not live under the plugin runner",
)
require(
    "agentModeCombo" in (repo / "src/Agent/UI/AgentDock.cpp").read_text(encoding="utf-8"),
    "dock must expose Ask/Plan/Edit",
)
require(
    "m_Book && m_Book->GetFolderKeeper() && m_Book->GetConstOPF()" in main_window,
    "UpdateAgentContext must not read metadata until OPF exists; empty Book::GetOPF() is null",
)
create_dock = main_window.split("void MainWindow::CreateAgentDock()", 1)[1].split(
    "void MainWindow::UpdateAgentContext()", 1
)[0]
require(
    "GetMetadataValues" not in create_dock,
    "CreateAgentDock runs during ExtendUI before LoadInitialFile; it must not read OPF metadata",
)
require(
    "UpdateAgentContext();" in main_window.split("void MainWindow::SetNewBook", 1)[1].split(
        "void MainWindow::ResourcesAddedOrDeletedOrMoved", 1
    )[0],
    "context chips must refresh after a real book is assigned",
)

print("agent dock contract ok")
