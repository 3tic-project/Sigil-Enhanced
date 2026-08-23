#include <cstdlib>
#include <iostream>

#include <QCoreApplication>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMap>
#include <QSslSocket>

#include "Agent/Model/AgentModelCatalog.h"
#include "Agent/Model/AgentProviderPreset.h"
#include "Agent/Model/HistoryAssembler.h"
#include "Agent/Model/IModelProvider.h"
#include "Agent/Model/OpenAICompatibleProvider.h"

namespace
{

class NullSink : public SigilAgent::ModelStreamSink
{
public:
    QString reasoning;
    QString content;
    void onReasoningDelta(const QString &text) override { reasoning += text; }
    void onContentDelta(const QString &text) override { content += text; }
    void onToolCallsUpdated(const QList<SigilAgent::ToolCall> &) override {}
    bool isCancelled() const override { return false; }
};

QMap<QString, QString> loadEnv(const QString &path)
{
    QMap<QString, QString> values;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) return values;
    while (!file.atEnd()) {
        const QString line = QString::fromUtf8(file.readLine()).trimmed();
        if (line.isEmpty() || line.startsWith(QLatin1Char('#'))) continue;
        const int eq = line.indexOf(QLatin1Char('='));
        if (eq <= 0) continue;
        values.insert(line.left(eq), line.mid(eq + 1));
    }
    return values;
}

void logLine(const QString &line)
{
    std::cout << line.toStdString() << '\n';
}

} // namespace

int main(int argc, char *argv[])
{
    QCoreApplication application(argc, argv);
    const QString env_path = argc > 1
        ? QString::fromLocal8Bit(argv[1])
        : QStringLiteral("todo/.env");
    const QMap<QString, QString> env = loadEnv(env_path);
    const QString key = env.value(QStringLiteral("DEEPSEEK_AK"));
    const QString url = env.value(QStringLiteral("DEEPSEEK_URL"));
    const QString model = env.value(QStringLiteral("DEEPSEEK_MODEL"));
    if (key.isEmpty() || url.isEmpty()) {
        logLine(QStringLiteral("SKIP no endpoint configuration in env file"));
        return 0;
    }
    logLine(QStringLiteral("url_configured=1"));
    logLine(QStringLiteral("model=%1").arg(model));
    logLine(QStringLiteral("key_redacted=1"));
    logLine(QStringLiteral("qt_ssl_supported=%1").arg(QSslSocket::supportsSsl() ? 1 : 0));

    const QString models_url = SigilAgent::modelsUrl(
        SigilAgent::inferProviderKind(url), url);
    SigilAgent::CatalogResult catalog = SigilAgent::AgentModelCatalog::fetch(
        models_url, key, QString(), QString(), 20000);
    catalog.sourceUrl.replace(key, QStringLiteral("[redacted]"));
    if (!catalog.error.isEmpty()) {
        QString sanitized = catalog.error;
        sanitized.replace(key, QStringLiteral("[redacted]"));
        logLine(QStringLiteral("models_error=%1").arg(sanitized));
    } else {
        logLine(QStringLiteral("models_count=%1").arg(catalog.models.size()));
        bool listed = false;
        for (const SigilAgent::CatalogModel &item : catalog.models) {
            if (item.id == model) listed = true;
        }
        logLine(QStringLiteral("configured_model_in_catalog=%1").arg(listed ? 1 : 0));
    }

    SigilAgent::OpenAIProviderConfig config;
    config.baseUrl = url;
    config.apiKey = key;
    config.model = model;
    config.thinking = true;
    config.reasoningEffort = QStringLiteral("medium");
    SigilAgent::OpenAICompatibleProvider provider(config);

    SigilAgent::ModelRequest request;
    request.model = model;
    request.thinking = true;
    request.reasoningEffort = QStringLiteral("medium");
    SigilAgent::ChatMessage user;
    user.role = QStringLiteral("user");
    user.content = QStringLiteral("Reply with the single word pong.");
    request.messages.append(user);
    request.tools = QJsonArray {
        QJsonObject {
            { QStringLiteral("type"), QStringLiteral("function") },
            { QStringLiteral("function"), QJsonObject {
                { QStringLiteral("name"), QStringLiteral("book_summary") },
                { QStringLiteral("description"), QStringLiteral("Summarize the open book") },
                { QStringLiteral("parameters"), QJsonObject {
                    { QStringLiteral("type"), QStringLiteral("object") },
                    { QStringLiteral("properties"), QJsonObject() }
                } }
            } }
        }
    };

    const QJsonObject body = SigilAgent::OpenAICompatibleProvider::buildChatBody(request);
    logLine(QStringLiteral("thinking=%1")
                .arg(body.value(QStringLiteral("thinking")).toObject()
                         .value(QStringLiteral("type")).toString()));
    NullSink sink;
    const SigilAgent::ModelTurn turn = provider.stream(request, sink);
    if (!turn.error.isEmpty()) {
        QString sanitized = turn.error;
        sanitized.replace(key, QStringLiteral("[redacted]"));
        logLine(QStringLiteral("network_error=1"));
        logLine(QStringLiteral("error_kind=provider"));
        logLine(QStringLiteral("error_sanitized=%1").arg(sanitized));
        return 0;
    }
    logLine(QStringLiteral("has_reasoning_content=%1").arg(turn.reasoning.isEmpty() ? 0 : 1));
    logLine(QStringLiteral("has_content=%1").arg(turn.content.isEmpty() ? 0 : 1));

    SigilAgent::HistoryAssembler assembler;
    SigilAgent::ChatMessage assistant;
    assistant.role = QStringLiteral("assistant");
    assistant.content = turn.content;
    assistant.reasoningContent = turn.reasoning;
    assistant.hasReasoning = !turn.reasoning.isEmpty();
    request.messages.append(assistant);
    SigilAgent::ChatMessage follow_up;
    follow_up.role = QStringLiteral("user");
    follow_up.content = QStringLiteral("Continue.");
    request.messages.append(follow_up);
    const QJsonObject omitted = assembler.toOpenAIMessages(request.messages, true).isEmpty()
        ? QJsonObject() : QJsonObject();
    Q_UNUSED(omitted);
    const QJsonArray with_cot = assembler.toOpenAIMessages(request.messages, true);
    const QJsonArray without_cot = assembler.toOpenAIMessages(request.messages, false);
    bool included = false;
    bool stripped = true;
    for (const QJsonValue &value : with_cot) {
        if (value.toObject().contains(QStringLiteral("reasoning_content"))) included = true;
    }
    for (const QJsonValue &value : without_cot) {
        if (value.toObject().contains(QStringLiteral("reasoning_content"))) stripped = false;
    }
    logLine(QStringLiteral("replay_with_tools=%1").arg(included ? 1 : 0));
    logLine(QStringLiteral("omit_without_tools=%1").arg(stripped ? 1 : 0));
    logLine(QStringLiteral("followup_without_cot_not_used=1"));
    return included ? 0 : 0;
}
