/************************************************************************
**
**  This file is part of Sigil-Enhanced.
**
*************************************************************************/

#pragma once
#ifndef SIGIL_AGENT_IMODEL_PROVIDER_H
#define SIGIL_AGENT_IMODEL_PROVIDER_H

#include <QJsonArray>
#include <QJsonObject>
#include <QString>

#include "Agent/AgentTypes.h"

namespace SigilAgent
{

enum class ReasoningProtocol {
    None,
    DeepSeek,
    OpenRouter
};

struct ModelCapabilities {
    bool streaming = true;
    bool toolCalling = true;
    bool parallelToolCalls = false;
    bool structuredOutput = false;
    bool reasoning = true;
    qsizetype maxContextTokens = 128000;
};

struct ModelRequest {
    QString model;
    QList<ChatMessage> messages;
    QJsonArray tools;
    QJsonObject historyContext;
    bool thinking = true;
    QString reasoningEffort = QStringLiteral("medium");
    ReasoningProtocol reasoningProtocol = ReasoningProtocol::DeepSeek;
    bool stream = true;
    bool includeUsage = true;
    int maxOutputTokens = 0;
    int timeoutMs = 120000;
};

class ModelStreamSink
{
public:
    virtual ~ModelStreamSink() = default;
    virtual void onReasoningDelta(const QString &text) = 0;
    virtual void onContentDelta(const QString &text) = 0;
    virtual void onToolCallsUpdated(const QList<ToolCall> &calls) = 0;
    virtual bool isCancelled() const = 0;
};

class IModelProvider
{
public:
    virtual ~IModelProvider() = default;
    virtual ModelCapabilities capabilities() const = 0;
    virtual ModelTurn stream(const ModelRequest &request, ModelStreamSink &sink) = 0;
    virtual QJsonArray debugTraces() const { return QJsonArray(); }
};

} // namespace SigilAgent

#endif
