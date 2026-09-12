/************************************************************************
**
**  This file is part of Sigil-Enhanced.
**
*************************************************************************/

#include "Agent/Core/AgentController.h"

#include "Agent/Tools/BookTools.h"
#include "Agent/Tools/DivParagraphTools.h"
#include "Agent/Tools/TocTools.h"

namespace SigilAgent
{

AgentController::AgentController() :
    m_gate(&m_cancellation)
{
}

void AgentController::rebuildTools()
{
    m_tools = ToolRegistry();
    if (m_workspace) {
        registerBookTools(&m_tools, m_workspace, &m_session);
        registerDivParagraphTools(&m_tools, m_workspace, &m_cancellation);
        registerTocTools(&m_tools, m_workspace, &m_cancellation);
    }
    m_runner = std::make_unique<AgentRunner>(
        &m_session, m_provider.get(), &m_tools, m_workspace, &m_policy, &m_gate, &m_cancellation);
    m_runner->setMode(m_mode);
    m_runner->setModel(m_model);
    m_runner->setThinking(m_thinkingEnabled, m_reasoningEffort);
}

bool AgentController::setWorkspace(IBookWorkspace *workspace)
{
    if (isRunning()) return false;
    m_workspace = workspace;
    rebuildTools();
    return true;
}

void AgentController::harvestProviderTraces()
{
    if (!m_provider) return;
    const QJsonArray traces = m_provider->debugTraces();
    for (const QJsonValue &value : traces) {
        m_httpTraces.append(value);
    }
    while (m_httpTraces.size() > 32) m_httpTraces.removeFirst();
}

bool AgentController::setProvider(std::unique_ptr<IModelProvider> provider)
{
    if (isRunning()) return false;
    harvestProviderTraces();
    m_provider = std::move(provider);
    rebuildTools();
    return true;
}

void AgentController::setMode(AgentMode mode)
{
    m_mode = mode;
    if (m_runner) m_runner->setMode(mode);
}

void AgentController::setModel(const QString &model)
{
    m_model = model;
    if (m_runner) m_runner->setModel(model);
}

void AgentController::setThinking(bool enabled, const QString &effort)
{
    m_thinkingEnabled = enabled;
    m_reasoningEffort = effort;
    if (m_runner) m_runner->setThinking(enabled, effort);
}

AgentSession *AgentController::session()
{
    return &m_session;
}

AgentRunner *AgentController::runner()
{
    return m_runner.get();
}

AgentCancellation *AgentController::cancellation()
{
    return &m_cancellation;
}

GuiApprovalGate *AgentController::approvalGate()
{
    return &m_gate;
}

IBookWorkspace *AgentController::workspace()
{
    return m_workspace;
}

ToolRegistry *AgentController::tools()
{
    return &m_tools;
}

bool AgentController::isRunning() const
{
    if (!m_runner) return false;
    const AgentRunState state = m_runner->state();
    return state != AgentRunState::Idle
        && state != AgentRunState::Completed
        && state != AgentRunState::Cancelled
        && state != AgentRunState::Failed;
}

AgentRunResult AgentController::send(const QString &text, const QStringList &handles)
{
    if (isRunning()) {
        AgentRunResult result;
        result.state = AgentRunState::Failed;
        result.error = QStringLiteral("An Agent run is already in progress");
        return result;
    }
    if (!m_runner) rebuildTools();
    AgentRunResult result = m_runner->runTurn(text, handles);
    if (m_resetSessionAfterRun) {
        resetSessionNow();
    }
    return result;
}

void AgentController::stop(AgentCancellationReason reason)
{
    m_cancellation.request(reason);
    m_gate.cancel();
}

void AgentController::newSession()
{
    if (isRunning()) {
        m_resetSessionAfterRun = true;
        stop(AgentCancellationReason::NewSession);
        return;
    }
    resetSessionNow();
}

void AgentController::resetSessionNow()
{
    if (m_workspace && m_workspace->hasOpenTransaction()) {
        m_workspace->rollbackTransaction();
    }
    m_resetSessionAfterRun = false;
    m_cancellation.reset();
    m_session.clear();
    m_httpTraces = QJsonArray();
    rebuildTools();
}

void AgentController::resolveApproval(const QString &toolCallId, bool approved)
{
    m_gate.resolve(toolCallId, approved);
}

QJsonArray AgentController::debugTraces() const
{
    QJsonArray traces = m_httpTraces;
    if (m_provider) {
        for (const QJsonValue &value : m_provider->debugTraces()) {
            traces.append(value);
        }
    }
    return traces;
}

} // namespace SigilAgent
