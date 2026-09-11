/************************************************************************
**
**  This file is part of Sigil-Enhanced.
**
*************************************************************************/

#include "Agent/AgentTypes.h"

#include <QJsonArray>
#include <QJsonDocument>

namespace SigilAgent
{

QString eventTypeName(AgentEventType type)
{
    switch (type) {
        case AgentEventType::SessionCreated: return QStringLiteral("session_created");
        case AgentEventType::UserMessage: return QStringLiteral("user_message");
        case AgentEventType::ContextAttached: return QStringLiteral("context_attached");
        case AgentEventType::ModelRequestStarted: return QStringLiteral("model_request_started");
        case AgentEventType::ModelRequestCompleted: return QStringLiteral("model_request_completed");
        case AgentEventType::ModelRequestFailed: return QStringLiteral("model_request_failed");
        case AgentEventType::AssistantDelta: return QStringLiteral("assistant_delta");
        case AgentEventType::AssistantMessage: return QStringLiteral("assistant_message");
        case AgentEventType::ToolRequested: return QStringLiteral("tool_requested");
        case AgentEventType::ToolApprovalRequested: return QStringLiteral("tool_approval_requested");
        case AgentEventType::ToolApproved: return QStringLiteral("tool_approved");
        case AgentEventType::ToolRejected: return QStringLiteral("tool_rejected");
        case AgentEventType::ToolStarted: return QStringLiteral("tool_started");
        case AgentEventType::ToolCompleted: return QStringLiteral("tool_completed");
        case AgentEventType::ToolFailed: return QStringLiteral("tool_failed");
        case AgentEventType::PlanCreated: return QStringLiteral("plan_created");
        case AgentEventType::TransactionPreviewed: return QStringLiteral("transaction_previewed");
        case AgentEventType::TransactionCommitted: return QStringLiteral("transaction_committed");
        case AgentEventType::TransactionRolledBack: return QStringLiteral("transaction_rolled_back");
        case AgentEventType::CheckpointCreated: return QStringLiteral("checkpoint_created");
        case AgentEventType::BookRevisionObserved: return QStringLiteral("book_revision_observed");
        case AgentEventType::RunStateChanged: return QStringLiteral("run_state_changed");
        case AgentEventType::SessionCancelled: return QStringLiteral("session_cancelled");
        case AgentEventType::Error: return QStringLiteral("error");
    }
    return QStringLiteral("error");
}

QString modeName(AgentMode mode)
{
    switch (mode) {
        case AgentMode::Ask: return QStringLiteral("ask");
        case AgentMode::Plan: return QStringLiteral("plan");
        case AgentMode::Edit: return QStringLiteral("edit");
        case AgentMode::Auto: return QStringLiteral("auto");
    }
    return QStringLiteral("ask");
}

QString runStateName(AgentRunState state)
{
    switch (state) {
        case AgentRunState::Idle: return QStringLiteral("idle");
        case AgentRunState::PreparingContext: return QStringLiteral("preparing_context");
        case AgentRunState::RequestingModel: return QStringLiteral("requesting_model");
        case AgentRunState::StreamingResponse: return QStringLiteral("streaming_response");
        case AgentRunState::AwaitingApproval: return QStringLiteral("awaiting_approval");
        case AgentRunState::ExecutingTools: return QStringLiteral("executing_tools");
        case AgentRunState::Validating: return QStringLiteral("validating");
        case AgentRunState::Completed: return QStringLiteral("completed");
        case AgentRunState::Cancelled: return QStringLiteral("cancelled");
        case AgentRunState::Failed: return QStringLiteral("failed");
    }
    return QStringLiteral("idle");
}

QString toolRiskName(ToolRisk risk)
{
    switch (risk) {
        case ToolRisk::Read: return QStringLiteral("read");
        case ToolRisk::ReversibleEdit: return QStringLiteral("reversible_edit");
        case ToolRisk::Bulk: return QStringLiteral("bulk");
        case ToolRisk::Destructive: return QStringLiteral("destructive");
    }
    return QStringLiteral("read");
}

AgentMode modeFromName(const QString &name)
{
    const QString lowered = name.trimmed().toLower();
    if (lowered == QLatin1String("plan")) return AgentMode::Plan;
    if (lowered == QLatin1String("edit")) return AgentMode::Edit;
    if (lowered == QLatin1String("auto")) return AgentMode::Auto;
    return AgentMode::Ask;
}

QJsonObject toolCallToJson(const ToolCall &call)
{
    return QJsonObject {
        { QStringLiteral("id"), call.id },
        { QStringLiteral("type"), QStringLiteral("function") },
        { QStringLiteral("function"), QJsonObject {
            { QStringLiteral("name"), call.name },
            { QStringLiteral("arguments"), call.argumentsJson }
        } }
    };
}

ToolCall toolCallFromJson(const QJsonObject &object)
{
    ToolCall call;
    call.id = object.value(QStringLiteral("id")).toString();
    const QJsonObject function = object.value(QStringLiteral("function")).toObject();
    call.name = function.value(QStringLiteral("name")).toString();
    if (call.name.isEmpty()) {
        call.name = object.value(QStringLiteral("name")).toString();
    }
    const QJsonValue arguments = function.contains(QStringLiteral("arguments"))
        ? function.value(QStringLiteral("arguments"))
        : object.value(QStringLiteral("arguments"));
    if (arguments.isObject() || arguments.isArray()) {
        call.argumentsJson = QString::fromUtf8(QJsonDocument::fromVariant(arguments.toVariant()).toJson(QJsonDocument::Compact));
    } else {
        call.argumentsJson = arguments.toString();
    }
    if (call.argumentsJson.isEmpty()) call.argumentsJson = QStringLiteral("{}");
    return call;
}

} // namespace SigilAgent
