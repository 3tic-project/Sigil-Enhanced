/************************************************************************
**
**  This file is part of Sigil-Enhanced.
**
*************************************************************************/

#include "Agent/Persistence/AgentSettings.h"

#include "Agent/AgentTypes.h"
#include "Misc/SettingsStore.h"

namespace SigilAgent
{

const char *AgentSettings::groupName()
{
    return "native_agent";
}

QString AgentSettings::baseUrl() const
{
    SettingsStore store;
    store.beginGroup(QLatin1String(groupName()));
    return store.value(QStringLiteral("base_url")).toString();
}

void AgentSettings::setBaseUrl(const QString &url)
{
    SettingsStore store;
    store.beginGroup(QLatin1String(groupName()));
    store.setValue(QStringLiteral("base_url"), url);
}

QString AgentSettings::apiKey() const
{
    SettingsStore store;
    store.beginGroup(QLatin1String(groupName()));
    return store.value(QStringLiteral("api_key")).toString();
}

void AgentSettings::setApiKey(const QString &key)
{
    SettingsStore store;
    store.beginGroup(QLatin1String(groupName()));
    store.setValue(QStringLiteral("api_key"), key);
}

QString AgentSettings::model() const
{
    SettingsStore store;
    store.beginGroup(QLatin1String(groupName()));
    const QString model = store.value(QStringLiteral("model")).toString();
    return model.isEmpty() ? QStringLiteral("deepseek-v4-flash") : model;
}

void AgentSettings::setModel(const QString &model)
{
    SettingsStore store;
    store.beginGroup(QLatin1String(groupName()));
    store.setValue(QStringLiteral("model"), model);
}

bool AgentSettings::thinkingEnabled() const
{
    SettingsStore store;
    store.beginGroup(QLatin1String(groupName()));
    return store.value(QStringLiteral("thinking"), true).toBool();
}

void AgentSettings::setThinkingEnabled(bool enabled)
{
    SettingsStore store;
    store.beginGroup(QLatin1String(groupName()));
    store.setValue(QStringLiteral("thinking"), enabled);
}

QString AgentSettings::reasoningEffort() const
{
    SettingsStore store;
    store.beginGroup(QLatin1String(groupName()));
    const QString effort = store.value(QStringLiteral("reasoning_effort")).toString();
    return effort.isEmpty() ? QStringLiteral("medium") : effort;
}

void AgentSettings::setReasoningEffort(const QString &effort)
{
    SettingsStore store;
    store.beginGroup(QLatin1String(groupName()));
    store.setValue(QStringLiteral("reasoning_effort"), effort);
}

AgentMode AgentSettings::defaultMode() const
{
    SettingsStore store;
    store.beginGroup(QLatin1String(groupName()));
    return modeFromName(store.value(QStringLiteral("mode")).toString());
}

void AgentSettings::setDefaultMode(AgentMode mode)
{
    SettingsStore store;
    store.beginGroup(QLatin1String(groupName()));
    store.setValue(QStringLiteral("mode"), modeName(mode));
}

OpenAIProviderConfig AgentSettings::providerConfig() const
{
    OpenAIProviderConfig config;
    config.baseUrl = baseUrl();
    config.apiKey = apiKey();
    config.model = model();
    config.thinking = thinkingEnabled();
    config.reasoningEffort = reasoningEffort();
    return config;
}

bool AgentSettings::looksLikeSecret(const QString &text)
{
    return text.startsWith(QLatin1String("sk-")) && text.size() > 12;
}

} // namespace SigilAgent
