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
book_browser_h = (repo / "src/MainUI/BookBrowser.h").read_text(encoding="utf-8")
book_browser_cpp = (repo / "src/MainUI/BookBrowser.cpp").read_text(encoding="utf-8")

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
    'addItem(tr("Auto"), QStringLiteral("auto"))' in dock_cpp,
    "dock must expose Auto mode",
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
    "agentTestConnectionButton" in settings_cpp
    and "probeAgentConnection(config, 15000)" in settings_cpp
    and "providerReadiness" in settings_cpp
    and "config.thinking = false" in settings_cpp,
    "Native Agent settings must run a bounded, locally validated Chat Completions probe",
)
require(
    "never sends book content or tools" in settings_cpp
    and "connectionTestState" in settings_cpp
    and "connectionTestHttpStatus" in settings_cpp
    and "currentConnectionFingerprint" in settings_cpp
    and "setConnectionTestVerification" in settings_cpp,
    "connection testing must disclose its scope and publish inspectable terminal state",
)
require(
    "agentExportButton" in dock_cpp and "exportDebugLogRequested" in dock_cpp,
    "dock must export the conversation and a debug log",
)
require(
    "agentProviderStatus" in dock_cpp
    and "ModelRequestCompleted" in dock_cpp
    and "ModelRequestFailed" in dock_cpp
    and "ModelRequestCancelled" in dock_cpp,
    "dock must distinguish provider setup, request success, failure, and cancellation",
)
require(
    "agentTechnicalDetailsToggle" in dock_cpp
    and "agentTechnicalDetails" in dock_cpp
    and "requestBookSessionId" in dock_cpp
    and "requestHandles" in dock_cpp,
    "protocol identifiers and immutable scope must live in expandable technical details",
)
require(
    "agentRetryButton" in dock_cpp
    and "m_lastSubmittedBookSessionId == m_bookSessionId" in dock_cpp
    and "m_requestStep == 1" in dock_cpp
    and "m_lastSubmittedHandles" in dock_cpp,
    "provider retry must reuse submitted scope and reject book changes or post-tool retries",
)
require(
    "agentChipSelectedFiles" in dock_cpp
    and "QButtonGroup" in dock_cpp
    and "setExclusive(true)" in dock_cpp
    and 'tr("Whole book")' in dock_cpp,
    "Agent scope must explicitly offer exclusive Selection/File/Selected files/Whole book choices",
)
configure_provider = main_window.split("void MainWindow::ConfigureAgentProvider()", 1)[1].split(
    "void MainWindow::CreateAgentDock()", 1
)[0]
require(
    "providerReadiness" in configure_provider
    and "verifiedConnectionAtMs" in configure_provider
    and "setProviderConfiguration" in configure_provider,
    "MainWindow must refresh safe provider readiness and matching test proof after Preferences changes and before Send",
)
require(
    "m_AgentController->isRunning()" in configure_provider
    and "m_AgentProviderReconfigurePending" in configure_provider,
    "provider replacement must be deferred until an in-flight Runner has returned",
)
close_event = main_window.split("void MainWindow::closeEvent", 1)[1].split(
    "void MainWindow::NewDefault", 1
)[0]
require(
    "AgentCancellationReason::WindowClosing" in close_event
    and "m_CloseAfterAgentRun" in close_event
    and "event->ignore()" in close_event,
    "window close must cancel and defer deletion while an Agent call is on the stack",
)
set_new_book = main_window.split("void MainWindow::SetNewBook", 1)[1].split(
    "void MainWindow::ResourcesAddedOrDeletedOrMoved", 1
)[0]
require(
    set_new_book.index("AgentCancellationReason::BookChanged")
        < set_new_book.index("m_AgentWorkspace->setBook"),
    "book replacement must cancel the bound Agent run before rebinding its workspace",
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
    "setSessionId" in create_dock and 'QStringLiteral("session_id")' in create_dock,
    "Agent technical details must track initial and renewed controller session ids",
)
require(
    "UpdateAgentContext();" in main_window.split("void MainWindow::SetNewBook", 1)[1].split(
        "void MainWindow::ResourcesAddedOrDeletedOrMoved", 1
    )[0],
    "context chips must refresh after a real book is assigned",
)
update_context = main_window.split("void MainWindow::UpdateAgentContext()", 1)[1].split(
    "void MainWindow::AgentSendRequested", 1
)[0]
require(
    "m_CurrentFileName" in update_context
    and "GetResourceList().size()" in update_context
    and "IsModified()" in update_context,
    "Agent book identity must include the file, resource count, and unsaved state",
)
require(
    "GetSelectionStart()" in update_context
    and "GetSelectionEnd()" in update_context
    and "qMax(cursor, cursor)" not in update_context,
    "Agent selection context must use the live editor range",
)
require(
    "AllSelectedResources()" in update_context
    and "setSelectedFiles" in update_context
    and "SelectedResourcesChanged" in main_window,
    "Book Browser selected resources must refresh the Agent selected-files scope",
)
require(
    "SelectedResourcesChanged" in book_browser_h
    and "m_SelectedResourcesNotificationPending" in book_browser_cpp
    and "QTimer::singleShot(0" in book_browser_cpp,
    "Book Browser multi-selection notifications must be coalesced",
)
require(
    "&Book::ModifiedStateChanged" in main_window
    and "&FlowTab::SelectionChanged" in main_window
    and "&TextTab::SelectionChanged" in main_window,
    "book save state and editor selection changes must refresh Agent context",
)

print("agent dock contract ok")
