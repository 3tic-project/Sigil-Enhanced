/************************************************************************
**
**  This file is part of Sigil-Enhanced.
**
*************************************************************************/

#pragma once
#ifndef SIGIL_AGENT_STREAMING_JSON_DECODER_H
#define SIGIL_AGENT_STREAMING_JSON_DECODER_H

#include <QByteArray>
#include <QJsonArray>
#include <QJsonObject>
#include <QList>
#include <QString>

#include "Agent/AgentTypes.h"

namespace SigilAgent
{

struct StreamDelta {
    QString reasoning;
    QString content;
    QList<ToolCall> toolCalls;
    QString finishReason;
    bool done = false;
};

class StreamingJsonDecoder
{
public:
    void reset();
    void feed(const QByteArray &chunk);
    QList<StreamDelta> takeDeltas();
    ModelTurn finish();
    QString error() const;
    QString reasoning() const;
    QString content() const;
    QList<ToolCall> toolCalls() const;

private:
    void parseLine(QByteArray line);
    void parsePayload(const QJsonObject &payload);
    void parseUsage(const QJsonObject &usage);
    void applyToolCallDelta(const QJsonArray &tool_calls);
    void appendReasoningDetails(const QJsonArray &details);

    QByteArray m_buffer;
    QString m_reasoning;
    QJsonArray m_reasoningDetails;
    QString m_content;
    QString m_finishReason;
    QString m_error;
    ModelUsage m_usage;
    QList<ToolCall> m_toolCalls;
    QList<StreamDelta> m_deltas;
    bool m_done = false;
};

} // namespace SigilAgent

#endif
