/************************************************************************
**
**  This file is part of Sigil-Enhanced.
**
*************************************************************************/

#include "Agent/Model/HistoryAssembler.h"

#include <QByteArray>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMap>
#include <QSet>

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
    if (include_tools && !message.reasoningDetails.isEmpty()) {
        object.insert(QStringLiteral("reasoning_details"), message.reasoningDetails);
    } else if (include_tools && message.hasReasoning) {
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
    return QJsonDocument(openAIMessage(message, include_tools || !message.reasoningDetails.isEmpty()))
        .toJson(QJsonDocument::Compact).size();
}

struct ProjectedMessage {
    int eventIndex = -1;
    ChatMessage message;
};

QString takeUtf8Bytes(const QByteArray &utf8, int max_bytes, bool from_end)
{
    if (max_bytes <= 0 || utf8.isEmpty()) return QString();
    if (utf8.size() <= max_bytes) return QString::fromUtf8(utf8);
    if (!from_end) {
        int bytes = max_bytes;
        while (bytes > 0
               && (static_cast<unsigned char>(utf8.at(bytes)) & 0xC0) == 0x80) {
            --bytes;
        }
        return QString::fromUtf8(utf8.constData(), bytes);
    }
    int start = utf8.size() - max_bytes;
    while (start < utf8.size()
           && (static_cast<unsigned char>(utf8.at(start)) & 0xC0) == 0x80) {
        ++start;
    }
    return QString::fromUtf8(utf8.constData() + start, utf8.size() - start);
}

QString projectToolResult(const QString &content)
{
    const QByteArray utf8 = content.toUtf8();
    if (utf8.size() <= MODEL_TOOL_RESULT_BYTES) return content;
    const QString marker = QStringLiteral(
        "\n[tool result truncated from %1 bytes; full output remains in the session log]\n")
                               .arg(utf8.size());
    const int budget = MODEL_TOOL_RESULT_BYTES - marker.toUtf8().size();
    const int head_budget = qMax(0, budget * 2 / 3);
    const int tail_budget = qMax(0, budget - head_budget);
    return takeUtf8Bytes(utf8, head_budget, false) + marker
        + takeUtf8Bytes(utf8, tail_budget, true);
}

struct HistoryTurn {
    QList<ChatMessage> messages;
    qint64 serializedBytes = 0;
};

ChatMessage currentTurnSummary(const QList<AgentEvent> &events)
{
    QMap<QString, int> completed;
    QMap<QString, int> failed;
    int commits = 0;
    qint64 last_book_revision = -1;
    QStringList committed_resources;
    QSet<QString> seen_resources;
    QString latest_restore_point;
    for (int i = events.size() - 1; i >= 0; --i) {
        const AgentEvent &event = events.at(i);
        if (event.type == AgentEventType::UserMessage) break;
        if (event.type == AgentEventType::ToolCompleted
            || event.type == AgentEventType::ToolFailed) {
            const QString name = event.payload.value(QStringLiteral("name"))
                                     .toString().left(80);
            if (event.type == AgentEventType::ToolCompleted) {
                ++completed[name];
            } else {
                const QString code = event.payload.value(QStringLiteral("code"))
                                         .toString().left(80);
                ++failed[name + QLatin1Char(':') + code];
            }
        } else if (event.type == AgentEventType::TransactionCommitted) {
            ++commits;
            if (last_book_revision < 0) {
                last_book_revision = event.payload.value(QStringLiteral("book_revision"))
                                         .toInteger(-1);
            }
            const QJsonArray resources = event.payload
                .value(QStringLiteral("resource_outcomes")).toObject()
                .value(QStringLiteral("resource_ids")).toArray();
            for (const QJsonValue &value : resources) {
                const QString id = value.toString().left(80);
                if (id.isEmpty() || seen_resources.contains(id)) continue;
                seen_resources.insert(id);
                if (committed_resources.size() < 8) committed_resources.append(id);
            }
            if (latest_restore_point.isEmpty()) {
                latest_restore_point = event.payload
                    .value(QStringLiteral("recovery")).toObject()
                    .value(QStringLiteral("checkpoint_id")).toString().left(80);
            }
        }
    }
    auto counts = [](const QMap<QString, int> &values) {
        QStringList parts;
        int shown = 0;
        for (auto it = values.cbegin(); it != values.cend() && shown < 12; ++it, ++shown) {
            parts.append(it.key() + QLatin1Char('=') + QString::number(it.value()));
        }
        if (values.size() > shown) parts.append(QStringLiteral("other=%1").arg(values.size() - shown));
        return parts.join(QStringLiteral(", "));
    };
    ChatMessage summary;
    summary.role = QStringLiteral("user");
    summary.content = QStringLiteral(
        "[Host summary of omitted current-turn history. Some assistant/tool exchanges "
        "were removed to fit the context budget. This is metadata, not the tool output. "
        "Do not assume a resource is unchanged or an edit succeeded; re-read the affected "
        "resource and revisions before writing. Completed tools: %1. Failed tools: %2. "
        "Committed transactions: %3. Last committed book revision: %4. "
        "Affected resources (up to 8): %5. Latest task restore point: %6.]")
        .arg(counts(completed), counts(failed), QString::number(commits),
             last_book_revision < 0 ? QStringLiteral("unknown")
                                    : QString::number(last_book_revision),
             committed_resources.join(QStringLiteral(", ")),
             latest_restore_point.isEmpty() ? QStringLiteral("none")
                                             : latest_restore_point);
    return summary;
}

ChatMessage checkpointSummary(const QList<AgentEvent> &events, int end_exclusive)
{
    QMap<QString, int> completed;
    QMap<QString, int> failed;
    int commits = 0;
    qint64 last_book_revision = -1;
    QStringList committed_resources;
    QSet<QString> seen_resources;
    QString latest_restore_point;
    const int end = qBound(0, end_exclusive, events.size());
    for (int i = 0; i < end; ++i) {
        const AgentEvent &event = events.at(i);
        if (event.type == AgentEventType::ToolCompleted
            || event.type == AgentEventType::ToolFailed) {
            const QString name = event.payload.value(QStringLiteral("name"))
                                     .toString().left(80);
            if (event.type == AgentEventType::ToolCompleted) {
                ++completed[name];
            } else {
                const QString code = event.payload.value(QStringLiteral("code"))
                                         .toString().left(80);
                ++failed[name + QLatin1Char(':') + code];
            }
        } else if (event.type == AgentEventType::TransactionCommitted) {
            ++commits;
            last_book_revision = event.payload.value(QStringLiteral("book_revision"))
                                     .toInteger(-1);
            const QJsonArray resources = event.payload
                .value(QStringLiteral("resource_outcomes")).toObject()
                .value(QStringLiteral("resource_ids")).toArray();
            for (const QJsonValue &value : resources) {
                const QString id = value.toString().left(80);
                if (id.isEmpty() || seen_resources.contains(id)) continue;
                seen_resources.insert(id);
                if (committed_resources.size() < 8) committed_resources.append(id);
            }
            latest_restore_point = event.payload
                .value(QStringLiteral("recovery")).toObject()
                .value(QStringLiteral("checkpoint_id")).toString().left(80);
        }
    }
    auto counts = [](const QMap<QString, int> &values) {
        QStringList parts;
        int shown = 0;
        for (auto it = values.cbegin(); it != values.cend() && shown < 12; ++it, ++shown) {
            parts.append(it.key() + QLatin1Char('=') + QString::number(it.value()));
        }
        if (values.size() > shown) {
            parts.append(QStringLiteral("other=%1").arg(values.size() - shown));
        }
        return parts.join(QStringLiteral(", "));
    };
    ChatMessage summary;
    summary.role = QStringLiteral("user");
    summary.content = QStringLiteral(
        "<compaction-summary>\n"
        "[Frozen host checkpoint of earlier tool rounds. Later requests reuse this "
        "text unchanged. Re-read a resource before writing when its result is not in "
        "the recent tail. Completed tools: %1. Failed tools: %2. "
        "Committed transactions: %3. Last committed book revision: %4. "
        "Affected resources (up to 8): %5. Latest task restore point: %6.]\n"
        "</compaction-summary>")
        .arg(counts(completed), counts(failed), QString::number(commits),
             last_book_revision < 0 ? QStringLiteral("unknown")
                                    : QString::number(last_book_revision),
             committed_resources.join(QStringLiteral(", ")),
             latest_restore_point.isEmpty() ? QStringLiteral("none")
                                             : latest_restore_point);
    return summary;
}

HistoryTurn compactCurrentTurn(const HistoryTurn &turn, const QList<AgentEvent> &events,
                               bool include_tools, int budget, int *omitted_count)
{
    if (budget <= 0 || turn.serializedBytes <= budget || turn.messages.isEmpty()) return turn;
    HistoryTurn compact;
    compact.messages.append(turn.messages.first());
    compact.serializedBytes = serializedMessageBytes(compact.messages.first(), include_tools);

    const ChatMessage summary = currentTurnSummary(events);
    compact.messages.append(summary);
    compact.serializedBytes += serializedMessageBytes(summary, include_tools);

    QList<HistoryTurn> groups;
    for (int i = 1; i < turn.messages.size(); ++i) {
        const ChatMessage &message = turn.messages.at(i);
        if (message.role == QLatin1String("assistant")) groups.append(HistoryTurn());
        if (groups.isEmpty()) continue;
        HistoryTurn &group = groups.last();
        if (message.role == QLatin1String("tool")) {
            bool matched = false;
            for (const ToolCall &call : group.messages.first().toolCalls) {
                if (call.id == message.toolCallId) {
                    matched = true;
                    break;
                }
            }
            if (!matched) continue;
        }
        group.messages.append(message);
        group.serializedBytes += serializedMessageBytes(message, include_tools);
    }
    int first_included_group = groups.size();
    for (int i = groups.size() - 1; i >= 0; --i) {
        if (compact.serializedBytes + groups.at(i).serializedBytes > budget) break;
        compact.serializedBytes += groups.at(i).serializedBytes;
        first_included_group = i;
    }
    for (int i = first_included_group; i < groups.size(); ++i) {
        compact.messages += groups.at(i).messages;
    }
    if (omitted_count) *omitted_count = turn.messages.size() - compact.messages.size() + 1;
    return compact;
}

QList<ProjectedMessage> projectEvents(const QList<AgentEvent> &events, bool include_tools)
{
    Q_UNUSED(include_tools);
    QList<ProjectedMessage> projected;
    for (int event_index = 0; event_index < events.size(); ++event_index) {
        const AgentEvent &event = events.at(event_index);
        if (event.type == AgentEventType::UserMessage) {
            ChatMessage message;
            message.role = QStringLiteral("user");
            message.content = event.payload.value(QStringLiteral("text")).toString();
            projected.append(ProjectedMessage { event_index, message });
            continue;
        }
        if (event.type == AgentEventType::AssistantMessage) {
            ChatMessage message;
            message.role = QStringLiteral("assistant");
            message.content = event.payload.value(QStringLiteral("content")).toString();
            const QString reasoning = event.payload.value(QStringLiteral("reasoning_content")).toString();
            message.reasoningDetails = event.payload.value(QStringLiteral("reasoning_details")).toArray();
            if (!reasoning.isEmpty()) {
                message.reasoningContent = reasoning;
                message.hasReasoning = true;
            }
            if (!message.reasoningDetails.isEmpty()) message.hasReasoning = true;
            message.toolCalls = toolCallsFromPayload(event.payload);
            projected.append(ProjectedMessage { event_index, message });
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
            message.content = projectToolResult(QString::fromUtf8(
                QJsonDocument(payload).toJson(QJsonDocument::Compact)));
            if (message.content.isEmpty()) message.content = QStringLiteral("{}");
            projected.append(ProjectedMessage { event_index, message });
        }
    }
    return projected;
}

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
        { QStringLiteral("current_turn_bytes"), currentTurnBytes },
        { QStringLiteral("current_turn_budget_bytes"), currentTurnBudgetBytes },
        { QStringLiteral("included_current_turn_bytes"), includedCurrentTurnBytes },
        { QStringLiteral("omitted_current_turn_messages"), omittedCurrentTurnMessages },
        { QStringLiteral("checkpoint_installed"), checkpointInstalled }
    };
}

QList<ChatMessage> HistoryAssembler::assemble(
    const QList<AgentEvent> &events,
    bool includeTools,
    int maxPreviousTurnBytes,
    HistoryAssemblyStats *stats,
    int maxCurrentTurnBytes,
    const HistoryCheckpoint *checkpoint) const
{
    QList<HistoryTurn> turns;
    QList<ProjectedMessage> projected;
    auto append_message = [&turns, &projected, includeTools](int event_index,
                                                             const ChatMessage &message) {
        projected.append(ProjectedMessage { event_index, message });
        if (turns.isEmpty()) turns.append(HistoryTurn());
        HistoryTurn &turn = turns.last();
        turn.messages.append(message);
        turn.serializedBytes += serializedMessageBytes(message, includeTools);
    };
    for (int event_index = 0; event_index < events.size(); ++event_index) {
        const AgentEvent &event = events.at(event_index);
        if (event.type == AgentEventType::UserMessage) {
            turns.append(HistoryTurn());
            ChatMessage message;
            message.role = QStringLiteral("user");
            message.content = event.payload.value(QStringLiteral("text")).toString();
            append_message(event_index, message);
            continue;
        }
        if (event.type == AgentEventType::AssistantMessage) {
            ChatMessage message;
            message.role = QStringLiteral("assistant");
            message.content = event.payload.value(QStringLiteral("content")).toString();
            const QString reasoning = event.payload.value(QStringLiteral("reasoning_content")).toString();
            message.reasoningDetails = event.payload.value(QStringLiteral("reasoning_details")).toArray();
            if (!reasoning.isEmpty()) {
                message.reasoningContent = reasoning;
                message.hasReasoning = true;
            }
            if (!message.reasoningDetails.isEmpty()) message.hasReasoning = true;
            message.toolCalls = toolCallsFromPayload(event.payload);
            append_message(event_index, message);
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
            message.content = projectToolResult(QString::fromUtf8(
                QJsonDocument(payload).toJson(QJsonDocument::Compact)));
            if (message.content.isEmpty()) message.content = QStringLiteral("{}");
            append_message(event_index, message);
            continue;
        }
    }

    if (checkpoint && checkpoint->installed) {
        QSet<int> omitted;
        for (int index : checkpoint->omittedEventIndexes) omitted.insert(index);
        QList<ChatMessage> messages;
        bool emitted = false;
        const auto emit_summary = [&messages, &emitted, checkpoint]() {
            if (emitted) return;
            ChatMessage summary;
            summary.role = QStringLiteral("user");
            summary.content = checkpoint->summary;
            messages.append(summary);
            emitted = true;
        };
        for (const ProjectedMessage &item : projected) {
            if (!emitted && item.eventIndex >= checkpoint->insertBeforeEvent) {
                emit_summary();
            }
            if (omitted.contains(item.eventIndex)) continue;
            messages.append(item.message);
        }
        if (!emitted) emit_summary();
        HistoryAssemblyStats assembled;
        assembled.checkpointInstalled = true;
        assembled.totalTurnCount = turns.size();
        assembled.includedTurnCount = turns.size();
        assembled.includedMessageCount = messages.size();
        assembled.omittedMessageCount = omitted.size();
        assembled.omittedCurrentTurnMessages = omitted.size();
        if (!turns.isEmpty()) {
            assembled.currentTurnBytes = turns.constLast().serializedBytes;
        }
        assembled.includedCurrentTurnBytes = 0;
        for (const ChatMessage &message : messages) {
            assembled.includedCurrentTurnBytes += serializedMessageBytes(message, includeTools);
        }
        if (stats) *stats = assembled;
        return messages;
    }

    HistoryAssemblyStats assembled;
    assembled.budgetBytes = qMax(0, maxPreviousTurnBytes);
    assembled.currentTurnBudgetBytes = qMax(0, maxCurrentTurnBytes);
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
        turns.last() = compactCurrentTurn(turns.constLast(), events, includeTools,
                                          assembled.currentTurnBudgetBytes,
                                          &assembled.omittedCurrentTurnMessages);
        assembled.includedCurrentTurnBytes = turns.constLast().serializedBytes;
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
    assembled.omittedMessageCount = total_message_count - assembled.includedMessageCount
                                    + (assembled.omittedCurrentTurnMessages > 0 ? 1 : 0);
    if (stats) *stats = assembled;
    return messages;
}

HistoryCheckpoint HistoryAssembler::planCheckpoint(
    const QList<AgentEvent> &events,
    bool includeTools,
    int keepTailBytes) const
{
    HistoryCheckpoint checkpoint;
    const QList<ProjectedMessage> projected = projectEvents(events, includeTools);
    struct Group {
        int firstEvent = -1;
        qint64 bytes = 0;
        bool user = false;
    };
    QList<Group> groups;
    for (const ProjectedMessage &item : projected) {
        const bool user = item.message.role == QLatin1String("user");
        const qint64 bytes = serializedMessageBytes(item.message, includeTools);
        if (user || groups.isEmpty()
            || item.message.role == QLatin1String("assistant")) {
            Group group;
            group.firstEvent = item.eventIndex;
            group.bytes = bytes;
            group.user = user;
            groups.append(group);
            continue;
        }
        if (item.message.role == QLatin1String("tool") && !groups.last().user) {
            bool matched = false;
            for (const ProjectedMessage &earlier : projected) {
                if (earlier.eventIndex != groups.last().firstEvent) continue;
                for (const ToolCall &call : earlier.message.toolCalls) {
                    if (call.id == item.message.toolCallId) matched = true;
                }
            }
            if (!matched) continue;
        }
        groups.last().bytes += bytes;
    }

    int first_kept_group = groups.size();
    qint64 kept_bytes = 0;
    bool kept_one = false;
    for (int i = groups.size() - 1; i >= 0; --i) {
        if (groups.at(i).user) continue;
        if (kept_one && kept_bytes + groups.at(i).bytes > keepTailBytes) break;
        kept_bytes += groups.at(i).bytes;
        first_kept_group = i;
        kept_one = true;
        if (kept_bytes >= keepTailBytes) break;
    }
    if (!kept_one) return checkpoint;

    bool drop = false;
    for (int i = 0; i < first_kept_group; ++i) {
        if (!groups.at(i).user) drop = true;
    }
    if (!drop) return checkpoint;

    const int tail_event = groups.at(first_kept_group).firstEvent;
    for (const ProjectedMessage &item : projected) {
        if (item.eventIndex >= tail_event) break;
        if (item.message.role == QLatin1String("user")) continue;
        checkpoint.omittedEventIndexes.append(item.eventIndex);
    }
    if (checkpoint.omittedEventIndexes.isEmpty()) return checkpoint;
    checkpoint.installed = true;
    checkpoint.insertBeforeEvent = tail_event;
    checkpoint.summary = checkpointSummary(events, tail_event).content;
    return checkpoint;
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
