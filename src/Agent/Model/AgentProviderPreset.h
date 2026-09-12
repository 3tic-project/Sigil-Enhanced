/************************************************************************
**
**  This file is part of Sigil-Enhanced.
**
*************************************************************************/

#pragma once
#ifndef SIGIL_AGENT_PROVIDER_PRESET_H
#define SIGIL_AGENT_PROVIDER_PRESET_H

#include <QList>
#include <QString>

#include "Agent/Model/IModelProvider.h"

namespace SigilAgent
{

enum class AgentProviderKind {
    DeepSeek,
    OpenCodeGo,
    OpenRouter,
    Custom
};

struct AgentProviderPreset {
    AgentProviderKind kind = AgentProviderKind::Custom;
    QString id;
    QString displayName;
    QString apiBase;
    QString defaultModel;
    ReasoningProtocol reasoningProtocol = ReasoningProtocol::None;
};

enum class AgentProviderSetupIssue {
    None,
    Endpoint,
    ApiKey,
    Model
};

struct AgentProviderReadiness {
    AgentProviderKind kind = AgentProviderKind::Custom;
    QString displayName;
    QString model;
    QString endpointHost;
    AgentProviderSetupIssue issue = AgentProviderSetupIssue::Endpoint;

    bool isConfigured() const { return issue == AgentProviderSetupIssue::None; }
};

QString providerKindName(AgentProviderKind kind);
AgentProviderKind providerKindFromName(const QString &name);
AgentProviderKind inferProviderKind(const QString &url);

AgentProviderPreset presetFor(AgentProviderKind kind);
QList<AgentProviderPreset> allProviderPresets();

QString trimTrailingSlash(QString url);
QString stripChatCompletionsPath(QString url);
QString chatCompletionsUrl(AgentProviderKind kind, const QString &overrideUrl);
QString modelsUrl(AgentProviderKind kind, const QString &overrideUrl);
ReasoningProtocol reasoningProtocolFor(AgentProviderKind kind, const QString &url);
AgentProviderReadiness providerReadiness(AgentProviderKind kind,
                                         const QString &chatUrl,
                                         bool apiKeyPresent,
                                         const QString &model);
QString providerConfigurationFingerprint(AgentProviderKind kind,
                                         const QString &chatUrl,
                                         const QString &apiKey,
                                         const QString &model);

QString agentHttpReferer();
QString agentHttpTitle();

} // namespace SigilAgent

#endif
