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
    void applyToolCallDelta(const QJsonArray &tool_calls);

    QByteArray m_buffer;
    QString m_reasoning;
    QString m_content;
    QString m_finishReason;
    QString m_error;
    QList<ToolCall> m_toolCalls;
    QList<StreamDelta> m_deltas;
    bool m_done = false;
};

} // namespace SigilAgent

#endif
