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

QJsonObject openAIMessage(const ChatMessage &message, bool include_tools)
{
    QJsonObject object;
    object.insert(QStringLiteral("role"), message.role);
    if (message.role == QLatin1String("tool")) {
        object.insert(QStringLiteral("tool_call_id"), message.toolCallId);
        object.insert(QStringLiteral("content"), message.content);
        return object;
    }

    object.insert(QStringLiteral("content"), message.content);
    if (include_tools && message.hasReasoning) {
        object.insert(QStringLiteral("reasoning_content"), message.reasoningContent);
    }
    if (!message.toolCalls.isEmpty()) {
        QJsonArray calls;
        for (const ToolCall &call : message.toolCalls) {
            calls.append(toolCallToJson(call));
        }
        object.insert(QStringLiteral("tool_calls"), calls);
    }
    return object;
}

qint64 serializedMessageBytes(const ChatMessage &message, bool include_tools)
{
    return QJsonDocument(openAIMessage(message, include_tools))
        .toJson(QJsonDocument::Compact).size();
}

struct HistoryTurn {
    QList<ChatMessage> messages;
    qint64 serializedBytes = 0;
};

} // namespace

QJsonObject HistoryAssemblyStats::toJson() const
{
    return QJsonObject {
        { QStringLiteral("limit_enabled"), budgetBytes > 0 },
        { QStringLiteral("budget_bytes"), budgetBytes },
        { QStringLiteral("total_turn_count"), totalTurnCount },
        { QStringLiteral("included_turn_count"), includedTurnCount },
        { QStringLiteral("omitted_turn_count"), omittedTurnCount },
        { QStringLiteral("included_message_count"), includedMessageCount },
        { QStringLiteral("omitted_message_count"), omittedMessageCount },
        { QStringLiteral("total_previous_turn_bytes"), totalPreviousTurnBytes },
        { QStringLiteral("included_previous_turn_bytes"),
          includedPreviousTurnBytes },
        { QStringLiteral("current_turn_bytes"), currentTurnBytes }
    };
}

QList<ChatMessage> HistoryAssembler::assemble(
    const QList<AgentEvent> &events,
    bool includeTools,
    int maxPreviousTurnBytes,
    HistoryAssemblyStats *stats) const
{
    QList<HistoryTurn> turns;
    auto append_message = [&turns, includeTools](const ChatMessage &message) {
        if (turns.isEmpty()) turns.append(HistoryTurn());
        HistoryTurn &turn = turns.last();
        turn.messages.append(message);
        turn.serializedBytes += serializedMessageBytes(message, includeTools);
    };
    for (const AgentEvent &event : events) {
        if (event.type == AgentEventType::UserMessage) {
            turns.append(HistoryTurn());
            ChatMessage message;
            message.role = QStringLiteral("user");
            message.content = event.payload.value(QStringLiteral("text")).toString();
            append_message(message);
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
            append_message(message);
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
            append_message(message);
            continue;
        }
    }

    HistoryAssemblyStats assembled;
    assembled.budgetBytes = qMax(0, maxPreviousTurnBytes);
    assembled.totalTurnCount = turns.size();
    int total_message_count = 0;
    for (int i = 0; i < turns.size(); ++i) {
        total_message_count += turns.at(i).messages.size();
        if (i + 1 < turns.size()) {
            assembled.totalPreviousTurnBytes += turns.at(i).serializedBytes;
        }
    }

    int first_included_turn = 0;
    if (!turns.isEmpty()) {
        assembled.currentTurnBytes = turns.constLast().serializedBytes;
    }
    if (assembled.budgetBytes > 0 && turns.size() > 1) {
        qint64 remaining = assembled.budgetBytes;
        first_included_turn = turns.size() - 1;
        for (int i = turns.size() - 2; i >= 0; --i) {
            if (turns.at(i).serializedBytes > remaining) break;
            remaining -= turns.at(i).serializedBytes;
            first_included_turn = i;
        }
    }

    QList<ChatMessage> messages;
    for (int i = first_included_turn; i < turns.size(); ++i) {
        messages += turns.at(i).messages;
        assembled.includedMessageCount += turns.at(i).messages.size();
        if (i + 1 < turns.size()) {
            assembled.includedPreviousTurnBytes += turns.at(i).serializedBytes;
        }
    }
    assembled.includedTurnCount = turns.size() - first_included_turn;
    assembled.omittedTurnCount = first_included_turn;
    assembled.omittedMessageCount = total_message_count - assembled.includedMessageCount;
    if (stats) *stats = assembled;
    return messages;
}

QJsonArray HistoryAssembler::toOpenAIMessages(const QList<ChatMessage> &messages, bool includeTools) const
{
    QJsonArray output;
    for (const ChatMessage &message : messages) {
        output.append(openAIMessage(message, includeTools));
    }
    return output;
}

} // namespace SigilAgent
