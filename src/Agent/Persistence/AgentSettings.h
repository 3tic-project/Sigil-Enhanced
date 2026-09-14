/************************************************************************
**
**  This file is part of Sigil-Enhanced.
**
*************************************************************************/

#pragma once
#ifndef SIGIL_AGENT_SETTINGS_H
#define SIGIL_AGENT_SETTINGS_H

#include <QJsonObject>
#include <QString>

#include "Agent/AgentTypes.h"
#include "Agent/Model/AgentProviderPreset.h"
#include "Agent/Model/OpenAICompatibleProvider.h"

namespace SigilAgent
{

class AgentSettings
{
public:
    static const char *groupName();

    QString provider() const;
    void setProvider(const QString &provider);
    AgentProviderKind providerKind() const;

    QString baseUrl() const;
    void setBaseUrl(const QString &url);

    QString apiKey() const;
    void setApiKey(const QString &key);

    QString model() const;
    void setModel(const QString &model);

    bool thinkingEnabled() const;
    void setThinkingEnabled(bool enabled);

    bool tokenUsageEnabled() const;
    void setTokenUsageEnabled(bool enabled);

    int historyPreviousTurnBudgetBytes() const;
    void setHistoryPreviousTurnBudgetBytes(int bytes);

    QString reasoningEffort() const;
    void setReasoningEffort(const QString &effort);

    AgentMode defaultMode() const;
    void setDefaultMode(AgentMode mode);

    bool modelSupportsReasoning() const;
    void setModelSupportsReasoning(bool enabled);
    bool modelSupportsTools() const;
    void setModelSupportsTools(bool enabled);
    qint64 modelContextLength() const;
    void setModelContextLength(qint64 tokens);

    QString catalogJson() const;
    void setCatalogJson(const QString &json);

    QJsonObject providerSecrets() const;
    void setProviderSecrets(const QJsonObject &secrets);
    QJsonObject providerModels() const;
    void setProviderModels(const QJsonObject &models);
    QJsonObject providerUrls() const;
    void setProviderUrls(const QJsonObject &urls);
    QJsonObject providerCatalogs() const;
    void setProviderCatalogs(const QJsonObject &catalogs);

    QString connectionTestFingerprint() const;
    qint64 connectionTestSucceededAtMs() const;
    void setConnectionTestVerification(const QString &fingerprint,
                                       qint64 succeededAtMs);
    qint64 verifiedConnectionAtMs() const;

    OpenAIProviderConfig providerConfig() const;

    static bool looksLikeSecret(const QString &text);
};

} // namespace SigilAgent

#endif
