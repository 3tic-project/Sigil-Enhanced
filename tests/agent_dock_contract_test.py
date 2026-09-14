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
runner_cpp = (repo / "src/Agent/Core/AgentRunner.cpp").read_text(encoding="utf-8")
controller_cpp = (repo / "src/Agent/Core/AgentController.cpp").read_text(
    encoding="utf-8"
)
agent_settings_cpp = (repo / "src/Agent/Persistence/AgentSettings.cpp").read_text(
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
    "m_catalogWatcher->setFuture(QtConcurrent::run" in settings_cpp
    and "modelRefreshState" in settings_cpp
    and "m_catalogCancelled->store(true" in settings_cpp
    and "30000, cancelled.get()" in settings_cpp,
    "model catalog refresh must run off the GUI thread and cancel safely with the settings page",
)
require(
    "agentTestConnectionButton" in settings_cpp
    and "QFutureWatcher" in settings_cpp
    and "QtConcurrent::run" in settings_cpp
    and "probeAgentConnection(config, 15000, cancelled.get())" in settings_cpp
    and "m_connectionCancelled->store(true" in settings_cpp
    and "providerReadiness" in settings_cpp
    and "config.thinking = false" in settings_cpp,
    "Native Agent settings must run a bounded, locally validated Chat Completions probe off the GUI thread",
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
    "agentTokenUsage" in settings_cpp
    and "stream_options.include_usage" in settings_cpp
    and "tokenUsageEnabled" in settings_cpp
    and "setTokenUsageEnabled" in settings_cpp,
    "Native Agent settings must expose and persist the optional token-usage request",
)
require(
    "setTokenUsage(settings.tokenUsageEnabled())" in main_window
    and 'QStringLiteral("usage_requested")' in runner_cpp
    and "modelUsageToJson(turn.usage)" in runner_cpp,
    "the saved usage setting and reported counts must reach request lifecycle events",
)
require(
    "agentHistoryBudgetKib" in settings_cpp
    and 'setSpecialValueText(tr("Unlimited"))' in settings_cpp
    and "previous complete conversation turns" in settings_cpp
    and "history_previous_turn_budget_bytes" in agent_settings_cpp,
    "Native Agent settings must disclose and persist the bounded previous-turn history budget",
)
require(
    "agentMaxModelSteps" in settings_cpp
    and "Maximum model steps per run" in settings_cpp
    and "rolls back any uncommitted staged transaction" in settings_cpp
    and "max_model_steps" in agent_settings_cpp
    and "setMaxModelSteps(settings.maxModelSteps())" in main_window,
    "Native Agent settings must disclose, persist, and apply the model-step safety limit",
)
require(
    "agentMaxToolCalls" in settings_cpp
    and "Maximum tool calls per run" in settings_cpp
    and "Rejects an entire model tool-call batch" in settings_cpp
    and "max_tool_calls" in agent_settings_cpp
    and "setMaxToolCalls(settings.maxToolCalls())" in main_window,
    "Native Agent settings must disclose, persist, and apply the tool-call safety limit",
)
require(
    "setHistoryPreviousTurnBudget(" in main_window
    and "settings.historyPreviousTurnBudgetBytes()" in main_window
    and "m_runner->setHistoryPreviousTurnBudget" in controller_cpp
    and "m_historyPreviousTurnBudgetBytes" in runner_cpp
    and 'QStringLiteral("history_context")' in runner_cpp,
    "the saved history budget and its audit statistics must reach every model request",
)
require(
    "captureRunEvent(event)" in dock_cpp
    and 'setProperty("runDurationMs"' in dock_cpp
    and "Whole run:" in dock_cpp,
    "technical details must consume whole-run timing separately from request timing",
)
require(
    'QStringLiteral("max_model_steps")' in runner_cpp
    and "m_runMaxModelSteps" in dock_h
    and "Model-step budget:" in dock_cpp
    and 'setProperty("runMaxModelSteps"' in dock_cpp,
    "technical details must disclose the configured model-step run budget",
)
require(
    "usage_summary" in runner_cpp
    and "reported_request_count" in runner_cpp
    and "Run token usage" in dock_cpp,
    "whole-run usage must preserve coverage and expose partial summaries honestly",
)
require(
    "response_timing" in runner_cpp
    and "modelResponseTimingToJson" in runner_cpp
    and "Response latency:" in dock_cpp
    and 'setProperty("firstByteMs"' in dock_cpp,
    "request events and technical details must expose measured response latency",
)
require(
    'QStringLiteral("history_context")' in runner_cpp
    and "m_requestHistoryContext" in dock_h
    and "Request history:" in dock_cpp
    and 'setProperty(\n        "historyBudgetBytes"' in dock_cpp
    and 'setProperty(\n        "historyCurrentTurnBytes"' in dock_cpp,
    "technical details must disclose each request's bounded history assembly",
)
require(
    'QStringLiteral("tool_context")' in runner_cpp
    and "m_requestToolContext" in dock_h
    and "Request tools:" in dock_cpp
    and 'setProperty(\n        "toolExposedCount"' in dock_cpp
    and 'setProperty(\n        "toolSavedSchemaBytes"' in dock_cpp,
    "technical details must disclose each request's mode-filtered tool catalog",
)
require(
    "AgentEventType::PlanCreated" in runner_cpp
    and "makePlanReviewCard" in dock_cpp
    and "openPlanResourceRequested" in dock_h
    and "openPlanResourceRequested" in main_window
    and "m_AgentWorkspace->bookSessionId() != book_session_id" in main_window
    and "Utility::URLDecodePath(book_path)" in main_window,
    "native plans must produce book-bound review cards and guarded resource navigation",
)
require(
    "matchesReviewedPlan" in dock_cpp
    and "m_reviewedPlans" in dock_h
    and 'setProperty("reviewedPlanMatched"' in dock_cpp,
    "native apply approval must fail closed when its displayed plan binding does not match",
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
    "event.type != SigilAgent::AgentEventType::AssistantDelta" in create_dock
    and "setState(AgentRunState::StreamingResponse)" in runner_cpp,
    "stream deltas must not repeat the full run-control refresh after streaming state is published",
)
require(
    "argument_overrides" in create_dock
    and "resolveApproval" in create_dock
    and 'QStringLiteral("selected_resource_ids")' in runner_cpp
    and "execution_arguments" in runner_cpp,
    "reviewed paragraph group choices must reach the Runner's restricted approval override path",
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
