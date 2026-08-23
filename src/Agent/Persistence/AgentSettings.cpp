/************************************************************************
**
**  This file is part of Sigil-Enhanced.
**
*************************************************************************/

#include "Agent/Persistence/AgentSettings.h"

#include <QJsonDocument>
#include <QJsonParseError>

#include "Agent/AgentTypes.h"
#include "Misc/SettingsStore.h"

namespace SigilAgent
{

namespace
{

QJsonObject readJsonObject(const QString &key)
{
    SettingsStore store;
    store.beginGroup(QLatin1String(AgentSettings::groupName()));
    const QByteArray raw = store.value(key).toByteArray();
    if (raw.isEmpty()) return QJsonObject();
    QJsonParseError error;
    const QJsonDocument document = QJsonDocument::fromJson(raw, &error);
    if (error.error != QJsonParseError::NoError || !document.isObject()) return QJsonObject();
    return document.object();
}

void writeJsonObject(const QString &key, const QJsonObject &object)
{
    SettingsStore store;
    store.beginGroup(QLatin1String(AgentSettings::groupName()));
    store.setValue(key, QJsonDocument(object).toJson(QJsonDocument::Compact));
}

} // namespace

const char *AgentSettings::groupName()
{
    return "native_agent";
}

QString AgentSettings::provider() const
{
    return providerKindName(providerKind());
}

void AgentSettings::setProvider(const QString &provider)
{
    SettingsStore store;
    store.beginGroup(QLatin1String(groupName()));
    store.setValue(QStringLiteral("provider"), provider);
}

AgentProviderKind AgentSettings::providerKind() const
{
    SettingsStore store;
    store.beginGroup(QLatin1String(groupName()));
    const QString stored = store.value(QStringLiteral("provider")).toString();
    if (stored.isEmpty()) return inferProviderKind(baseUrl());
    return providerKindFromName(stored);
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
    if (!model.isEmpty()) return model;
    return presetFor(providerKind()).defaultModel;
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

bool AgentSettings::modelSupportsReasoning() const
{
    SettingsStore store;
    store.beginGroup(QLatin1String(groupName()));
    const bool deepseek_default = providerKind() == AgentProviderKind::DeepSeek;
    return store.value(QStringLiteral("model_supports_reasoning"), deepseek_default).toBool();
}

void AgentSettings::setModelSupportsReasoning(bool enabled)
{
    SettingsStore store;
    store.beginGroup(QLatin1String(groupName()));
    store.setValue(QStringLiteral("model_supports_reasoning"), enabled);
}

bool AgentSettings::modelSupportsTools() const
{
    SettingsStore store;
    store.beginGroup(QLatin1String(groupName()));
    return store.value(QStringLiteral("model_supports_tools"), true).toBool();
}

void AgentSettings::setModelSupportsTools(bool enabled)
{
    SettingsStore store;
    store.beginGroup(QLatin1String(groupName()));
    store.setValue(QStringLiteral("model_supports_tools"), enabled);
}

qint64 AgentSettings::modelContextLength() const
{
    SettingsStore store;
    store.beginGroup(QLatin1String(groupName()));
    return store.value(QStringLiteral("model_context_length"), 0).toLongLong();
}

void AgentSettings::setModelContextLength(qint64 tokens)
{
    SettingsStore store;
    store.beginGroup(QLatin1String(groupName()));
    store.setValue(QStringLiteral("model_context_length"), tokens);
}

QString AgentSettings::catalogJson() const
{
    SettingsStore store;
    store.beginGroup(QLatin1String(groupName()));
    return QString::fromUtf8(store.value(QStringLiteral("catalog_json")).toByteArray());
}

void AgentSettings::setCatalogJson(const QString &json)
{
    SettingsStore store;
    store.beginGroup(QLatin1String(groupName()));
    store.setValue(QStringLiteral("catalog_json"), json.toUtf8());
}

QJsonObject AgentSettings::providerSecrets() const
{
    return readJsonObject(QStringLiteral("provider_secrets"));
}

void AgentSettings::setProviderSecrets(const QJsonObject &secrets)
{
    writeJsonObject(QStringLiteral("provider_secrets"), secrets);
}

QJsonObject AgentSettings::providerModels() const
{
    return readJsonObject(QStringLiteral("provider_models"));
}

void AgentSettings::setProviderModels(const QJsonObject &models)
{
    writeJsonObject(QStringLiteral("provider_models"), models);
}

QJsonObject AgentSettings::providerUrls() const
{
    return readJsonObject(QStringLiteral("provider_urls"));
}

void AgentSettings::setProviderUrls(const QJsonObject &urls)
{
    writeJsonObject(QStringLiteral("provider_urls"), urls);
}

QJsonObject AgentSettings::providerCatalogs() const
{
    return readJsonObject(QStringLiteral("provider_catalogs"));
}

void AgentSettings::setProviderCatalogs(const QJsonObject &catalogs)
{
    writeJsonObject(QStringLiteral("provider_catalogs"), catalogs);
}

OpenAIProviderConfig AgentSettings::providerConfig() const
{
    const AgentProviderKind kind = providerKind();
    OpenAIProviderConfig config;
    config.baseUrl = chatCompletionsUrl(kind, baseUrl());
    config.apiKey = apiKey();
    config.model = model();
    config.thinking = thinkingEnabled();
    config.reasoningEffort = reasoningEffort();
    config.reasoningProtocol = reasoningProtocolFor(kind, config.baseUrl);
    if (kind == AgentProviderKind::OpenRouter && !modelSupportsReasoning()) {
        config.reasoningProtocol = ReasoningProtocol::None;
        config.thinking = false;
    }
    if (kind == AgentProviderKind::OpenRouter) {
        config.httpReferer = agentHttpReferer();
        config.httpTitle = agentHttpTitle();
    }
    return config;
}

bool AgentSettings::looksLikeSecret(const QString &text)
{
    return text.startsWith(QLatin1String("sk-")) && text.size() > 12;
}

} // namespace SigilAgent
