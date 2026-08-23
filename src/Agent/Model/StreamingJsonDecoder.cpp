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

void StreamingJsonDecoder::reset()
{
    m_buffer.clear();
    m_reasoning.clear();
    m_content.clear();
    m_finishReason.clear();
    m_error.clear();
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
    turn.content = m_content;
    turn.toolCalls = m_toolCalls;
    turn.finishReason = m_finishReason;
    turn.error = m_error;
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
