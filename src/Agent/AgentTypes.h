/************************************************************************
**
**  This file is part of Sigil-Enhanced.
**
*************************************************************************/

#pragma once
#ifndef SIGIL_AGENT_TYPES_H
#define SIGIL_AGENT_TYPES_H

#include <QJsonArray>
#include <QJsonObject>
#include <QList>
#include <QMetaType>
#include <QString>

namespace SigilAgent
{

enum class AgentMode {
    Ask,
    Plan,
    Edit,
    Auto
};

enum class AgentRunState {
    Idle,
    PreparingContext,
    RequestingModel,
    StreamingResponse,
    AwaitingApproval,
    ExecutingTools,
    Validating,
    Completed,
    Cancelled,
    Failed
};

enum class AgentEventType {
    SessionCreated,
    UserMessage,
    ContextAttached,
    ModelRequestStarted,
    ModelRequestCompleted,
    ModelRequestFailed,
    AssistantDelta,
    AssistantMessage,
    ToolRequested,
    ToolApprovalRequested,
    ToolApproved,
    ToolRejected,
    ToolStarted,
    ToolCompleted,
    ToolFailed,
    PlanCreated,
    TransactionPreviewed,
    TransactionCommitted,
    TransactionRolledBack,
    CheckpointCreated,
    BookRevisionObserved,
    BookTargetChanged,
    RunStateChanged,
    SessionCancelled,
    Error
};

enum class ToolRisk {
    Read,
    ReversibleEdit,
    Bulk,
    Destructive
};

enum class PermissionAction {
    Allow,
    Ask,
    Deny
};

struct ToolCall {
    QString id;
    QString name;
    QString argumentsJson;
};

struct ChatMessage {
    QString role;
    QString content;
    QString reasoningContent;
    QString toolCallId;
    QList<ToolCall> toolCalls;
    bool hasReasoning = false;
};

struct AgentEvent {
    QString id;
    AgentEventType type = AgentEventType::Error;
    qint64 timestampMs = 0;
    QJsonObject payload;
};

struct ModelTurn {
    QString reasoning;
    QString content;
    QList<ToolCall> toolCalls;
    QString finishReason;
    QString error;
};

QString eventTypeName(AgentEventType type);
QString modeName(AgentMode mode);
QString runStateName(AgentRunState state);
QString toolRiskName(ToolRisk risk);
AgentMode modeFromName(const QString &name);
QJsonObject toolCallToJson(const ToolCall &call);
ToolCall toolCallFromJson(const QJsonObject &object);

} // namespace SigilAgent

Q_DECLARE_METATYPE(SigilAgent::AgentEvent)

#endif // SIGIL_AGENT_TYPES_H
