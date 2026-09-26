/************************************************************************
**
**  This file is part of Sigil-Enhanced.
**
*************************************************************************/

#include "Agent/Model/AgentProviderPreset.h"

#include <QCryptographicHash>
#include <QUrl>

namespace SigilAgent
{

QString providerKindName(AgentProviderKind kind)
{
    switch (kind) {
        case AgentProviderKind::DeepSeek: return QStringLiteral("deepseek");
        case AgentProviderKind::OpenCodeGo: return QStringLiteral("opencode_go");
        case AgentProviderKind::OpenRouter: return QStringLiteral("openrouter");
        case AgentProviderKind::Custom: return QStringLiteral("custom");
    }
    return QStringLiteral("custom");
}

AgentProviderKind providerKindFromName(const QString &name)
{
    const QString lowered = name.trimmed().toLower();
    if (lowered == QLatin1String("deepseek")) return AgentProviderKind::DeepSeek;
    if (lowered == QLatin1String("opencode_go")
        || lowered == QLatin1String("opencode-go")
        || lowered == QLatin1String("opencode")) {
        return AgentProviderKind::OpenCodeGo;
    }
    if (lowered == QLatin1String("openrouter")) return AgentProviderKind::OpenRouter;
    if (lowered == QLatin1String("custom")) return AgentProviderKind::Custom;
    return AgentProviderKind::DeepSeek;
}

AgentProviderKind inferProviderKind(const QString &url)
{
    const QString lowered = url.trimmed().toLower();
    if (lowered.contains(QLatin1String("openrouter.ai"))) return AgentProviderKind::OpenRouter;
    if (lowered.contains(QLatin1String("opencode.ai"))) return AgentProviderKind::OpenCodeGo;
    if (lowered.contains(QLatin1String("deepseek.com"))) return AgentProviderKind::DeepSeek;
    if (lowered.isEmpty()) return AgentProviderKind::DeepSeek;
    return AgentProviderKind::Custom;
}

AgentProviderPreset presetFor(AgentProviderKind kind)
{
    AgentProviderPreset preset;
    preset.kind = kind;
    preset.id = providerKindName(kind);
    switch (kind) {
        case AgentProviderKind::DeepSeek:
            preset.displayName = QStringLiteral("DeepSeek");
            preset.apiBase = QStringLiteral("https://api.deepseek.com");
            preset.defaultModel = QStringLiteral("deepseek-v4-flash");
            preset.reasoningProtocol = ReasoningProtocol::DeepSeek;
            break;
        case AgentProviderKind::OpenCodeGo:
            preset.displayName = QStringLiteral("OpenCode Go");
            preset.apiBase = QStringLiteral("https://opencode.ai/zen/go/v1");
            preset.reasoningProtocol = ReasoningProtocol::None;
            break;
        case AgentProviderKind::OpenRouter:
            preset.displayName = QStringLiteral("OpenRouter");
            preset.apiBase = QStringLiteral("https://openrouter.ai/api/v1");
            preset.reasoningProtocol = ReasoningProtocol::OpenRouter;
            break;
        case AgentProviderKind::Custom:
            preset.displayName = QStringLiteral("Custom (OpenAI-compatible)");
            preset.reasoningProtocol = ReasoningProtocol::None;
            break;
    }
    return preset;
}

QList<AgentProviderPreset> allProviderPresets()
{
    return {
        presetFor(AgentProviderKind::DeepSeek),
        presetFor(AgentProviderKind::OpenCodeGo),
        presetFor(AgentProviderKind::OpenRouter),
        presetFor(AgentProviderKind::Custom)
    };
}

QString trimTrailingSlash(QString url)
{
    url = url.trimmed();
    while (url.endsWith(QLatin1Char('/'))) url.chop(1);
    return url;
}

QString stripChatCompletionsPath(QString url)
{
    url = trimTrailingSlash(url);
    const QString suffix = QStringLiteral("/chat/completions");
    if (url.endsWith(suffix, Qt::CaseInsensitive)) {
        url.chop(suffix.size());
        url = trimTrailingSlash(url);
    }
    return url;
}

QString chatCompletionsUrl(AgentProviderKind kind, const QString &overrideUrl)
{
    QString url = trimTrailingSlash(overrideUrl);
    if (url.isEmpty()) {
        url = trimTrailingSlash(presetFor(kind).apiBase);
    }
    if (url.isEmpty()) return QString();
    if (url.endsWith(QLatin1String("/chat/completions"), Qt::CaseInsensitive)) return url;
    return url + QStringLiteral("/chat/completions");
}

QString modelsUrl(AgentProviderKind kind, const QString &overrideUrl)
{
    QString root = trimTrailingSlash(overrideUrl);
    if (root.isEmpty()) {
        root = trimTrailingSlash(presetFor(kind).apiBase);
    } else {
        root = stripChatCompletionsPath(root);
    }
    if (root.isEmpty()) return QString();
    QString url = root + QStringLiteral("/models");
    if (kind == AgentProviderKind::OpenRouter && !url.contains(QLatin1Char('?'))) {
        url += QStringLiteral("?supported_parameters=tools");
    }
    return url;
}

ReasoningProtocol reasoningProtocolFor(AgentProviderKind kind, const QString &url)
{
    switch (kind) {
        case AgentProviderKind::DeepSeek:
            return ReasoningProtocol::DeepSeek;
        case AgentProviderKind::OpenRouter:
            return ReasoningProtocol::OpenRouter;
        case AgentProviderKind::OpenCodeGo:
            return ReasoningProtocol::None;
        case AgentProviderKind::Custom: {
            const AgentProviderKind inferred = inferProviderKind(url);
            if (inferred == AgentProviderKind::Custom) return ReasoningProtocol::None;
            return reasoningProtocolFor(inferred, url);
        }
    }
    return ReasoningProtocol::None;
}

AgentProviderReadiness providerReadiness(AgentProviderKind kind,
                                         const QString &chat_url,
                                         bool api_key_present,
                                         const QString &model)
{
    AgentProviderReadiness readiness;
    readiness.kind = kind;
    readiness.displayName = presetFor(kind).displayName;
    readiness.model = model.trimmed();

    const QUrl url(chat_url.trimmed());
    const QString scheme = url.scheme().toLower();
    const bool valid_endpoint = url.isValid()
        && (scheme == QLatin1String("http") || scheme == QLatin1String("https"))
        && !url.host().isEmpty();
    if (valid_endpoint) {
        readiness.endpointHost = url.host();
        if (readiness.endpointHost.contains(QLatin1Char(':'))) {
            readiness.endpointHost = QStringLiteral("[%1]").arg(readiness.endpointHost);
        }
        const int port = url.port(-1);
        if (port > 0) readiness.endpointHost += QStringLiteral(":%1").arg(port);
    }

    if (!valid_endpoint) {
        readiness.issue = AgentProviderSetupIssue::Endpoint;
    } else if (!api_key_present) {
        readiness.issue = AgentProviderSetupIssue::ApiKey;
    } else if (readiness.model.isEmpty()) {
        readiness.issue = AgentProviderSetupIssue::Model;
    } else {
        readiness.issue = AgentProviderSetupIssue::None;
    }
    return readiness;
}

QString providerConfigurationFingerprint(AgentProviderKind kind,
                                         const QString &chat_url,
                                         const QString &api_key,
                                         const QString &model)
{
    QByteArray serialized;
    const auto append_field = [&serialized](const QString &value) {
        const QByteArray bytes = value.toUtf8();
        serialized.append(QByteArray::number(bytes.size()));
        serialized.append(':');
        serialized.append(bytes);
    };
    append_field(providerKindName(kind));
    append_field(chatCompletionsUrl(kind, chat_url));
    append_field(api_key);
    append_field(model);
    return QString::fromLatin1(
        QCryptographicHash::hash(serialized, QCryptographicHash::Sha256).toHex());
}

QString agentHttpReferer()
{
    return QStringLiteral("https://github.com/3tic-project/Sigil-Enhanced");
}

QString agentHttpTitle()
{
    return QStringLiteral("Sigil-Enhanced Native Agent");
}

QString agentUserAgent()
{
    return QStringLiteral("Sigil-Enhanced-Native-Agent/1.0");
}

QString openCodeGoEndpointForModel(const QString &model)
{
    // OpenCode Go's /models currently returns ids without their API protocol.
    // Keep this mapping in sync with https://opencode.ai/docs/zh-cn/go/#api-端点.
    QString id = model.trimmed().toLower();
    if (id.startsWith(QLatin1String("opencode-go/"))) {
        id.remove(0, QStringLiteral("opencode-go/").size());
    }
    if (id.startsWith(QLatin1String("grok-"))
        || id == QLatin1String("gpt-5.6-luna")
        || id == QLatin1String("gpt-6-luna")
        || id.startsWith(QLatin1String("muse-spark-"))) {
        return QStringLiteral("/responses");
    }
    if (id.startsWith(QLatin1String("minimax-m"))
        || id.startsWith(QLatin1String("qwen3."))) {
        return QStringLiteral("/messages");
    }
    return QStringLiteral("/chat/completions");
}

} // namespace SigilAgent
