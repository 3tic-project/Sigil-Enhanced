/************************************************************************
**
**  This file is part of Sigil-Enhanced.
**
*************************************************************************/

#pragma once
#ifndef SIGIL_AGENT_OPENAI_COMPATIBLE_PROVIDER_H
#define SIGIL_AGENT_OPENAI_COMPATIBLE_PROVIDER_H

#include <QJsonArray>
#include <QJsonObject>
#include <QString>
#include <QStringList>

#include "Agent/Model/IModelProvider.h"

class QNetworkAccessManager;

namespace SigilAgent
{

constexpr int DEFAULT_FIRST_TOKEN_TIMEOUT_MS = 180000;
constexpr int MAX_FIRST_TOKEN_TIMEOUT_MS = 1800000;

struct OpenAIProviderConfig {
    QString baseUrl;
    QString apiKey;
    QString model;
    bool thinking = true;
    bool requestUsage = true;
    QString reasoningEffort = QStringLiteral("medium");
    ReasoningProtocol reasoningProtocol = ReasoningProtocol::DeepSeek;
    QString httpReferer;
    QString httpTitle;
    bool openCodeGo = false;
    QString userAgent;
    QStringList supportedReasoningEfforts;
    QString defaultReasoningEffort;
    bool reasoningEffortSelectable = true;
    int firstTokenTimeoutMs = DEFAULT_FIRST_TOKEN_TIMEOUT_MS;
};

class OpenAICompatibleProvider : public IModelProvider
{
public:
    explicit OpenAICompatibleProvider(OpenAIProviderConfig config);
    void setConfig(const OpenAIProviderConfig &config);
    OpenAIProviderConfig config() const;
    ModelCapabilities capabilities() const override;
    ModelTurn stream(const ModelRequest &request, ModelStreamSink &sink) override;
    QJsonArray debugTraces() const override;

    static QJsonObject buildChatBody(const ModelRequest &request);

private:
    void recordTrace(const QJsonObject &trace);

    OpenAIProviderConfig m_config;
    QString m_fallbackSessionId;
    QJsonArray m_traces;
};

} // namespace SigilAgent

#endif
