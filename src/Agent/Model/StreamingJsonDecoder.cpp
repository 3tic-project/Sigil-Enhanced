/************************************************************************
**
**  This file is part of Sigil-Enhanced.
**
*************************************************************************/

#include "Agent/Model/StreamingJsonDecoder.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

namespace SigilAgent
{

namespace
{

qint64 nonNegativeInteger(const QJsonValue &value)
{
    if (!value.isDouble()) return -1;
    const qint64 result = value.toInteger(-1);
    return result >= 0 ? result : -1;
}

qint64 usageValue(const QJsonObject &usage,
                  const QString &primary,
                  const QString &alias = QString())
{
    qint64 result = nonNegativeInteger(usage.value(primary));
    if (result < 0 && !alias.isEmpty()) {
        result = nonNegativeInteger(usage.value(alias));
    }
    return result;
}

} // namespace

void StreamingJsonDecoder::reset()
{
    m_buffer.clear();
    m_reasoning.clear();
    m_reasoningDetails = QJsonArray();
    m_content.clear();
    m_finishReason.clear();
    m_error.clear();
    m_usage = ModelUsage();
    m_toolCalls.clear();
    m_deltas.clear();
    m_done = false;
}

void StreamingJsonDecoder::feed(const QByteArray &chunk)
{
    if (chunk.isEmpty() || !m_error.isEmpty()) return;
    m_buffer += chunk;
    while (true) {
        const int newline = m_buffer.indexOf('\n');
        if (newline < 0) break;
        QByteArray line = m_buffer.left(newline);
        m_buffer.remove(0, newline + 1);
        if (line.endsWith('\r')) line.chop(1);
        parseLine(line);
        if (!m_error.isEmpty()) break;
    }
}

QList<StreamDelta> StreamingJsonDecoder::takeDeltas()
{
    QList<StreamDelta> deltas = m_deltas;
    m_deltas.clear();
    return deltas;
}

ModelTurn StreamingJsonDecoder::finish()
{
    if (m_error.isEmpty() && !m_buffer.trimmed().isEmpty()) {
        parseLine(m_buffer);
        m_buffer.clear();
    }
    ModelTurn turn;
    turn.reasoning = m_reasoning;
    turn.reasoningDetails = m_reasoningDetails;
    turn.content = m_content;
    turn.toolCalls = m_toolCalls;
    turn.finishReason = m_finishReason;
    turn.error = m_error;
    turn.usage = m_usage;
    if (turn.finishReason.isEmpty()) {
        turn.finishReason = turn.toolCalls.isEmpty()
            ? QStringLiteral("stop") : QStringLiteral("tool_calls");
    }
    return turn;
}

QString StreamingJsonDecoder::error() const
{
    return m_error;
}

QString StreamingJsonDecoder::reasoning() const
{
    return m_reasoning;
}

QString StreamingJsonDecoder::content() const
{
    return m_content;
}

QList<ToolCall> StreamingJsonDecoder::toolCalls() const
{
    return m_toolCalls;
}

void StreamingJsonDecoder::parseLine(QByteArray line)
{
    line = line.trimmed();
    if (line.isEmpty() || line.startsWith(':')) return;

    if (line.startsWith("data:")) {
        line = line.mid(5).trimmed();
    }
    if (line == "[DONE]" || line == "data: [DONE]") {
        m_done = true;
        StreamDelta delta;
        delta.done = true;
        delta.finishReason = m_finishReason;
        m_deltas.append(delta);
        return;
    }
    if (line.isEmpty()) return;

    QJsonParseError parse_error;
    const QJsonDocument document = QJsonDocument::fromJson(line, &parse_error);
    if (parse_error.error != QJsonParseError::NoError || !document.isObject()) {
        m_error = QStringLiteral("Invalid JSON event: %1").arg(parse_error.errorString());
        return;
    }
    parsePayload(document.object());
}

void StreamingJsonDecoder::parsePayload(const QJsonObject &payload)
{
    if (payload.contains(QStringLiteral("error"))) {
        const QJsonValue error = payload.value(QStringLiteral("error"));
        if (error.isObject()) {
            m_error = error.toObject().value(QStringLiteral("message")).toString();
        } else {
            m_error = error.toString();
        }
        if (m_error.isEmpty()) m_error = QStringLiteral("Provider error");
        return;
    }

    if (payload.value(QStringLiteral("usage")).isObject()) {
        parseUsage(payload.value(QStringLiteral("usage")).toObject());
    }

    const QJsonArray choices = payload.value(QStringLiteral("choices")).toArray();
    if (choices.isEmpty()) return;
    const QJsonObject choice = choices.at(0).toObject();
    QJsonObject delta = choice.value(QStringLiteral("delta")).toObject();
    if (delta.isEmpty() && choice.contains(QStringLiteral("message"))) {
        delta = choice.value(QStringLiteral("message")).toObject();
    }

    StreamDelta emitted;
    QString reasoning = delta.value(QStringLiteral("reasoning_content")).toString();
    if (reasoning.isEmpty()) {
        const QJsonValue reasoning_value = delta.value(QStringLiteral("reasoning"));
        if (reasoning_value.isString()) {
            reasoning = reasoning_value.toString();
        } else if (reasoning_value.isObject()) {
            reasoning = reasoning_value.toObject().value(QStringLiteral("content")).toString();
        }
    }
    const QJsonArray details = delta.value(QStringLiteral("reasoning_details")).toArray();
    if (!details.isEmpty()) {
        appendReasoningDetails(details);
        if (reasoning.isEmpty()) {
            for (const QJsonValue &value : details) {
                const QJsonObject detail = value.toObject();
                if (detail.value(QStringLiteral("type")).toString()
                    == QLatin1String("reasoning.text")) {
                    reasoning += detail.value(QStringLiteral("text")).toString();
                } else if (detail.value(QStringLiteral("type")).toString()
                           == QLatin1String("reasoning.summary")) {
                    reasoning += detail.value(QStringLiteral("summary")).toString();
                }
            }
        }
    }
    const QString content = delta.value(QStringLiteral("content")).toString();
    if (!reasoning.isEmpty()) {
        m_reasoning += reasoning;
        emitted.reasoning = reasoning;
    }
    if (!content.isEmpty()) {
        m_content += content;
        emitted.content = content;
    }
    if (delta.contains(QStringLiteral("tool_calls")) && delta.value(QStringLiteral("tool_calls")).isArray()) {
        applyToolCallDelta(delta.value(QStringLiteral("tool_calls")).toArray());
        emitted.toolCalls = m_toolCalls;
    }

    const QString finish = choice.value(QStringLiteral("finish_reason")).toString();
    if (!finish.isEmpty() && finish != QLatin1String("null")) {
        m_finishReason = finish;
        emitted.finishReason = finish;
    }

    if (!emitted.reasoning.isEmpty() || !emitted.content.isEmpty()
        || !emitted.toolCalls.isEmpty() || !emitted.finishReason.isEmpty()) {
        m_deltas.append(emitted);
    }
}

void StreamingJsonDecoder::appendReasoningDetails(const QJsonArray &details)
{
    for (const QJsonValue &value : details) {
        if (!value.isObject()) continue;
        const QJsonObject incoming = value.toObject();
        const int index = incoming.value(QStringLiteral("index")).toInt(-1);
        int existing_index = -1;
        if (index >= 0) {
            for (int i = 0; i < m_reasoningDetails.size(); ++i) {
                if (m_reasoningDetails.at(i).toObject()
                        .value(QStringLiteral("index")).toInt(-1) == index) {
                    existing_index = i;
                    break;
                }
            }
        }
        if (existing_index < 0) {
            m_reasoningDetails.append(incoming);
            continue;
        }
        QJsonObject merged = m_reasoningDetails.at(existing_index).toObject();
        for (auto it = incoming.begin(); it != incoming.end(); ++it) {
            if ((it.key() == QLatin1String("text")
                 || it.key() == QLatin1String("summary")
                 || it.key() == QLatin1String("data")) && it.value().isString()) {
                QString combined = merged.value(it.key()).toString();
                combined += it.value().toString();
                merged.insert(it.key(), combined);
            } else if (!it.value().isNull()) {
                merged.insert(it.key(), it.value());
            }
        }
        m_reasoningDetails.replace(existing_index, merged);
    }
}

void StreamingJsonDecoder::parseUsage(const QJsonObject &usage)
{
    const qint64 input = usageValue(
        usage, QStringLiteral("prompt_tokens"), QStringLiteral("input_tokens"));
    const qint64 output = usageValue(
        usage, QStringLiteral("completion_tokens"), QStringLiteral("output_tokens"));
    const qint64 total = usageValue(usage, QStringLiteral("total_tokens"));
    if (input >= 0) m_usage.inputTokens = input;
    if (output >= 0) m_usage.outputTokens = output;
    if (total >= 0) {
        m_usage.totalTokens = total;
    } else if (m_usage.inputTokens >= 0 && m_usage.outputTokens >= 0) {
        m_usage.totalTokens = m_usage.inputTokens + m_usage.outputTokens;
    }

    QJsonObject input_details =
        usage.value(QStringLiteral("prompt_tokens_details")).toObject();
    if (input_details.isEmpty()) {
        input_details = usage.value(QStringLiteral("input_tokens_details")).toObject();
    }
    qint64 cached = usageValue(
        input_details, QStringLiteral("cached_tokens"),
        QStringLiteral("cached_input_tokens"));
    if (cached < 0) {
        cached = usageValue(
            usage, QStringLiteral("prompt_cache_hit_tokens"),
            QStringLiteral("cached_input_tokens"));
    }
    if (cached >= 0) m_usage.cachedInputTokens = cached;

    QJsonObject output_details =
        usage.value(QStringLiteral("completion_tokens_details")).toObject();
    if (output_details.isEmpty()) {
        output_details = usage.value(QStringLiteral("output_tokens_details")).toObject();
    }
    qint64 reasoning = usageValue(
        output_details, QStringLiteral("reasoning_tokens"));
    if (reasoning < 0) {
        reasoning = usageValue(usage, QStringLiteral("reasoning_tokens"));
    }
    if (reasoning >= 0) m_usage.reasoningTokens = reasoning;
}

void StreamingJsonDecoder::applyToolCallDelta(const QJsonArray &tool_calls)
{
    for (const QJsonValue &value : tool_calls) {
        const QJsonObject object = value.toObject();
        int index = object.value(QStringLiteral("index")).toInt(-1);
        if (index < 0) index = m_toolCalls.size();
        while (m_toolCalls.size() <= index) {
            ToolCall placeholder;
            placeholder.argumentsJson = QString();
            m_toolCalls.append(placeholder);
        }
        ToolCall &call = m_toolCalls[index];
        if (object.contains(QStringLiteral("id"))) {
            const QString id = object.value(QStringLiteral("id")).toString();
            if (!id.isEmpty()) call.id = id;
        }
        const QJsonObject function = object.value(QStringLiteral("function")).toObject();
        if (function.contains(QStringLiteral("name"))) {
            const QString name = function.value(QStringLiteral("name")).toString();
            if (!name.isEmpty()) call.name = name;
        }
        if (object.contains(QStringLiteral("name")) && call.name.isEmpty()) {
            call.name = object.value(QStringLiteral("name")).toString();
        }
        if (function.contains(QStringLiteral("arguments"))) {
            call.argumentsJson += function.value(QStringLiteral("arguments")).toString();
        }
        if (call.argumentsJson.isNull()) call.argumentsJson = QString();
    }
}

} // namespace SigilAgent
