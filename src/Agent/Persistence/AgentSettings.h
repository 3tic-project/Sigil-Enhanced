/************************************************************************
**
**  This file is part of Sigil-Enhanced.
**
*************************************************************************/

#pragma once
#ifndef SIGIL_AGENT_SETTINGS_H
#define SIGIL_AGENT_SETTINGS_H

#include <QString>

#include "Agent/AgentTypes.h"
#include "Agent/Model/OpenAICompatibleProvider.h"

namespace SigilAgent
{

class AgentSettings
{
public:
    static const char *groupName();

    QString baseUrl() const;
    void setBaseUrl(const QString &url);

    QString apiKey() const;
    void setApiKey(const QString &key);

    QString model() const;
    void setModel(const QString &model);

    bool thinkingEnabled() const;
    void setThinkingEnabled(bool enabled);

    QString reasoningEffort() const;
    void setReasoningEffort(const QString &effort);

    AgentMode defaultMode() const;
    void setDefaultMode(AgentMode mode);

    OpenAIProviderConfig providerConfig() const;

    static bool looksLikeSecret(const QString &text);
};

} // namespace SigilAgent

#endif
