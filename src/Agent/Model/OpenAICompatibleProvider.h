/************************************************************************
**
**  This file is part of Sigil-Enhanced.
**
*************************************************************************/

#pragma once
#ifndef SIGIL_AGENT_OPENAI_COMPATIBLE_PROVIDER_H
#define SIGIL_AGENT_OPENAI_COMPATIBLE_PROVIDER_H

#include <QJsonObject>
#include <QString>

#include "Agent/Model/IModelProvider.h"

class QNetworkAccessManager;

namespace SigilAgent
{

struct OpenAIProviderConfig {
    QString baseUrl;
    QString apiKey;
    QString model;
    bool thinking = true;
    QString reasoningEffort = QStringLiteral("medium");
};

class OpenAICompatibleProvider : public IModelProvider
{
public:
    explicit OpenAICompatibleProvider(OpenAIProviderConfig config);
    void setConfig(const OpenAIProviderConfig &config);
    OpenAIProviderConfig config() const;
    ModelCapabilities capabilities() const override;
    ModelTurn stream(const ModelRequest &request, ModelStreamSink &sink) override;

    static QJsonObject buildChatBody(const ModelRequest &request);

private:
    OpenAIProviderConfig m_config;
};

} // namespace SigilAgent

#endif
