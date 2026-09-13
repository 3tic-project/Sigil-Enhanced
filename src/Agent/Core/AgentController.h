/************************************************************************
**
**  This file is part of Sigil-Enhanced.
**
*************************************************************************/

#pragma once
#ifndef SIGIL_AGENT_CONTROLLER_H
#define SIGIL_AGENT_CONTROLLER_H

#include <memory>

#include <QJsonArray>

#include "Agent/Core/AgentCancellation.h"
#include "Agent/Core/AgentRunner.h"
#include "Agent/Core/AgentSession.h"
#include "Agent/Execution/IBookWorkspace.h"
#include "Agent/Model/IModelProvider.h"
#include "Agent/Security/PermissionPolicy.h"
#include "Agent/Tools/ToolRegistry.h"
#include "Agent/UI/GuiApprovalGate.h"

namespace SigilAgent
{

class AgentController
{
public:
    AgentController();

    bool setWorkspace(IBookWorkspace *workspace);
    bool setProvider(std::unique_ptr<IModelProvider> provider);
    void setMode(AgentMode mode);
    void setModel(const QString &model);
    void setThinking(bool enabled, const QString &effort);
    void setTokenUsage(bool enabled);

    AgentSession *session();
    AgentRunner *runner();
    AgentCancellation *cancellation();
    GuiApprovalGate *approvalGate();
    IBookWorkspace *workspace();
    ToolRegistry *tools();
    bool isRunning() const;

    AgentRunResult send(const QString &text, const QStringList &handles);
    BookOpResult restoreTask(const QString &checkpointId,
                             const QString &expectedBookSessionId);
    void stop(AgentCancellationReason reason = AgentCancellationReason::UserStop);
    void newSession();
    void resolveApproval(const QString &toolCallId, bool approved,
                         const QJsonObject &argumentOverrides = QJsonObject());
    QJsonArray debugTraces() const;

private:
    void rebuildTools();
    void resetSessionNow();

    AgentSession m_session;
    void harvestProviderTraces();

    AgentCancellation m_cancellation;
    PermissionPolicy m_policy;
    GuiApprovalGate m_gate;
    ToolRegistry m_tools;
    IBookWorkspace *m_workspace = nullptr;
    std::unique_ptr<IModelProvider> m_provider;
    std::unique_ptr<AgentRunner> m_runner;
    QJsonArray m_httpTraces;
    AgentMode m_mode = AgentMode::Ask;
    QString m_model;
    bool m_thinkingEnabled = true;
    bool m_tokenUsageEnabled = true;
    QString m_reasoningEffort = QStringLiteral("medium");
    bool m_resetSessionAfterRun = false;
};

} // namespace SigilAgent

#endif
