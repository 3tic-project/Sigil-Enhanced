/************************************************************************
**
**  This file is part of Sigil-Enhanced.
**
*************************************************************************/

#pragma once
#ifndef SIGIL_AGENT_RUNNER_H
#define SIGIL_AGENT_RUNNER_H

#include <QElapsedTimer>

#include "Agent/AgentTypes.h"
#include "Agent/Core/AgentCancellation.h"
#include "Agent/Core/AgentSession.h"
#include "Agent/Core/PromptAssembler.h"
#include "Agent/Execution/IBookWorkspace.h"
#include "Agent/Model/IModelProvider.h"
#include "Agent/Security/PermissionPolicy.h"
#include "Agent/Tools/ToolRegistry.h"

namespace SigilAgent
{

struct AgentRunResult {
    AgentRunState state = AgentRunState::Idle;
    QString finalText;
    QStringList toolNames;
    QString error;
};

class AgentRunner
{
public:
    AgentRunner(AgentSession *session,
                IModelProvider *provider,
                ToolRegistry *tools,
                IBookWorkspace *workspace,
                PermissionPolicy *policy,
                IApprovalGate *gate,
                AgentCancellation *cancellation);

    void setMode(AgentMode mode);
    AgentMode mode() const;
    void setModel(const QString &model);
    void setThinking(bool enabled, const QString &effort);
    void setTokenUsage(bool enabled);
    void setMaxSteps(int steps);
    AgentRunState state() const;

    AgentRunResult runTurn(const QString &user_text, const QStringList &handles = QStringList());

private:
    class SessionSink : public ModelStreamSink
    {
    public:
        SessionSink(AgentSession *session, AgentCancellation *cancellation);
        void onReasoningDelta(const QString &text) override;
        void onContentDelta(const QString &text) override;
        void onToolCallsUpdated(const QList<ToolCall> &calls) override;
        bool isCancelled() const override;

    private:
        AgentSession *m_session;
        AgentCancellation *m_cancellation;
    };

    void setState(AgentRunState state);
    ToolResult executeTool(const ToolCall &call);
    void publishToolOutcome(const ToolCall &call, const ToolResult &result);
    void rollbackOpenWork();
    bool bookTargetMatchesRun() const;
    AgentRunResult cancelRun();
    AgentRunResult failBookTargetChanged(const QString &stage);
    QJsonObject parseArguments(const QString &json) const;
    void accumulateRunUsage(const ModelUsage &usage);
    QJsonObject runUsageSummary() const;

    AgentSession *m_session;
    IModelProvider *m_provider;
    ToolRegistry *m_tools;
    IBookWorkspace *m_workspace;
    PermissionPolicy *m_policy;
    IApprovalGate *m_gate;
    AgentCancellation *m_cancellation;
    PromptAssembler m_prompts;
    AgentMode m_mode = AgentMode::Ask;
    AgentRunState m_state = AgentRunState::Idle;
    QString m_model;
    bool m_thinking = true;
    bool m_tokenUsage = true;
    QString m_effort = QStringLiteral("medium");
    int m_maxSteps = 24;
    QString m_runBookSessionId;
    QString m_runId;
    QElapsedTimer m_runTimer;
    int m_runModelSteps = 0;
    int m_runToolCalls = 0;
    ModelUsage m_runUsage;
    int m_runUsageReportedRequests = 0;
    int m_runInputUsageRequests = 0;
    int m_runOutputUsageRequests = 0;
    int m_runTotalUsageRequests = 0;
    int m_runCachedUsageRequests = 0;
    int m_runReasoningUsageRequests = 0;
    bool m_runUsageRequested = true;
    bool m_runTimingActive = false;
};

} // namespace SigilAgent

#endif
