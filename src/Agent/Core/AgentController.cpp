/************************************************************************
**
**  This file is part of Sigil-Enhanced.
**
*************************************************************************/

#include "Agent/Core/AgentController.h"

#include "Agent/Tools/BookTools.h"

namespace SigilAgent
{

AgentController::AgentController() :
    m_gate(&m_cancellation)
{
}

void AgentController::rebuildTools()
{
    m_tools = ToolRegistry();
    if (m_workspace) registerBookTools(&m_tools, m_workspace, &m_session);
    m_runner = std::make_unique<AgentRunner>(
        &m_session, m_provider.get(), &m_tools, m_workspace, &m_policy, &m_gate, &m_cancellation);
}

void AgentController::setWorkspace(IBookWorkspace *workspace)
{
    m_workspace = workspace;
    rebuildTools();
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

void AgentController::setProvider(std::unique_ptr<IModelProvider> provider)
{
    harvestProviderTraces();
    m_provider = std::move(provider);
    rebuildTools();
}

void AgentController::setMode(AgentMode mode)
{
    if (m_runner) m_runner->setMode(mode);
}

void AgentController::setModel(const QString &model)
{
    if (m_runner) m_runner->setModel(model);
}

void AgentController::setThinking(bool enabled, const QString &effort)
{
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

AgentRunResult AgentController::send(const QString &text, const QStringList &handles)
{
    if (!m_runner) rebuildTools();
    return m_runner->runTurn(text, handles);
}

void AgentController::stop()
{
    m_cancellation.request();
    m_gate.cancel();
}

void AgentController::newSession()
{
    stop();
    if (m_workspace && m_workspace->hasOpenTransaction()) {
        m_workspace->rollbackTransaction();
    }
    m_cancellation.reset();
    m_session.clear();
    m_httpTraces = QJsonArray();
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
