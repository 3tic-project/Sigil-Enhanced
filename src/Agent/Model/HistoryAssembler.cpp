/************************************************************************
**
**  This file is part of Sigil-Enhanced.
**
*************************************************************************/

#include "Agent/Model/HistoryAssembler.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

namespace SigilAgent
{

namespace
{

QList<ToolCall> toolCallsFromPayload(const QJsonObject &payload)
{
    QList<ToolCall> calls;
    const QJsonArray array = payload.value(QStringLiteral("tool_calls")).toArray();
    for (const QJsonValue &value : array) {
        calls.append(toolCallFromJson(value.toObject()));
    }
    return calls;
}

} // namespace

QList<ChatMessage> HistoryAssembler::assemble(const QList<AgentEvent> &events, bool includeTools) const
{
    QList<ChatMessage> messages;
    for (const AgentEvent &event : events) {
        if (event.type == AgentEventType::UserMessage) {
            ChatMessage message;
            message.role = QStringLiteral("user");
            message.content = event.payload.value(QStringLiteral("text")).toString();
            messages.append(message);
            continue;
        }
        if (event.type == AgentEventType::AssistantMessage) {
            ChatMessage message;
            message.role = QStringLiteral("assistant");
            message.content = event.payload.value(QStringLiteral("content")).toString();
            const QString reasoning = event.payload.value(QStringLiteral("reasoning_content")).toString();
            if (!reasoning.isEmpty()) {
                message.reasoningContent = reasoning;
                message.hasReasoning = true;
            }
            message.toolCalls = toolCallsFromPayload(event.payload);
            messages.append(message);
            continue;
        }
        if (event.type == AgentEventType::ToolCompleted
            || event.type == AgentEventType::ToolFailed) {
            ChatMessage message;
            message.role = QStringLiteral("tool");
            message.toolCallId = event.payload.value(QStringLiteral("tool_call_id")).toString();
            if (message.toolCallId.isEmpty()) {
                message.toolCallId = event.payload.value(QStringLiteral("id")).toString();
            }
            QJsonObject payload = event.payload;
            if (payload.contains(QStringLiteral("result"))) {
                const QJsonValue result = payload.value(QStringLiteral("result"));
                if (result.isObject()) payload = result.toObject();
            }
            message.content = QString::fromUtf8(QJsonDocument(payload).toJson(QJsonDocument::Compact));
            if (message.content.isEmpty()) message.content = QStringLiteral("{}");
            messages.append(message);
            continue;
        }
    }
    Q_UNUSED(includeTools);
    return messages;
}

QJsonArray HistoryAssembler::toOpenAIMessages(const QList<ChatMessage> &messages, bool includeTools) const
{
    QJsonArray output;
    for (const ChatMessage &message : messages) {
        QJsonObject object;
        object.insert(QStringLiteral("role"), message.role);
        if (message.role == QLatin1String("tool")) {
            object.insert(QStringLiteral("tool_call_id"), message.toolCallId);
            object.insert(QStringLiteral("content"), message.content);
        } else {
            object.insert(QStringLiteral("content"), message.content);
            if (includeTools && message.hasReasoning) {
                object.insert(QStringLiteral("reasoning_content"), message.reasoningContent);
            }
            if (!message.toolCalls.isEmpty()) {
                QJsonArray calls;
                for (const ToolCall &call : message.toolCalls) {
                    calls.append(toolCallToJson(call));
                }
                object.insert(QStringLiteral("tool_calls"), calls);
            }
        }
        output.append(object);
    }
    return output;
}

} // namespace SigilAgent
