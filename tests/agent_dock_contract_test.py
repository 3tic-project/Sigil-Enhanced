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
dock_cpp = (repo / "src/Agent/UI/AgentDock.cpp").read_text(encoding="utf-8")
settings_cpp = (repo / "src/Dialogs/PreferenceWidgets/AgentSettingsWidget.cpp").read_text(
    encoding="utf-8"
)
require(
    "agentModeCombo" in dock_cpp,
    "dock must expose Ask/Plan/Edit",
)
require(
    "agentModelEdit" not in dock_cpp,
    "model name must not be typed in the Agent dock",
)
require(
    "agentProviderCombo" in settings_cpp
    and "OpenCode Go" in settings_cpp
    and "OpenRouter" in settings_cpp
    and "Refresh models" in settings_cpp,
    "Native Agent settings must offer DeepSeek, OpenCode Go, OpenRouter, and a models fetch",
)
require(
    "agentExportButton" in dock_cpp and "exportDebugLogRequested" in dock_cpp,
    "dock must export the conversation and a debug log",
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
