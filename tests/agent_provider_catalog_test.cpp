#include <atomic>
#include <cstdlib>
#include <iostream>

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFutureWatcher>
#include <QJsonArray>
#include <QJsonDocument>
#include <QHostAddress>
#include <QJsonObject>
#include <QRegularExpression>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>
#include <QtConcurrent>

#include "Agent/Core/AgentSession.h"
#include "Agent/Model/AgentConnectionProbe.h"
#include "Agent/Model/AgentModelCatalog.h"
#include "Agent/Model/AgentProviderPreset.h"
#include "Agent/Model/OpenAICompatibleProvider.h"
#include "Agent/Persistence/AgentSessionExport.h"

namespace
{

void Require(bool condition, const char *message)
{
    if (!condition) {
        std::cerr << message << '\n';
        std::exit(EXIT_FAILURE);
    }
}

class NullSink : public SigilAgent::ModelStreamSink
{
public:
    void onReasoningDelta(const QString &) override {}
    void onContentDelta(const QString &) override {}
    void onToolCallsUpdated(const QList<SigilAgent::ToolCall> &) override {}
    bool isCancelled() const override { return false; }
};

class RecordingSink : public SigilAgent::ModelStreamSink
{
public:
    void onReasoningDelta(const QString &text) override { reasoning += text; }
    void onContentDelta(const QString &text) override { content += text; }
    void onToolCallsUpdated(const QList<SigilAgent::ToolCall> &) override {}
    bool isCancelled() const override { return false; }

    QString reasoning;
    QString content;
};

} // namespace

int main(int argc, char *argv[])
{
    QCoreApplication application(argc, argv);
    using namespace SigilAgent;

    Require(chatCompletionsUrl(AgentProviderKind::DeepSeek, QString())
                == QStringLiteral("https://api.deepseek.com/chat/completions"),
            "DeepSeek chat URL must come from the preset");
    Require(modelsUrl(AgentProviderKind::DeepSeek, QStringLiteral("https://api.deepseek.com/chat/completions"))
                == QStringLiteral("https://api.deepseek.com/models"),
            "DeepSeek models URL must strip /chat/completions");
    Require(chatCompletionsUrl(AgentProviderKind::OpenCodeGo, QString())
                == QStringLiteral("https://opencode.ai/zen/go/v1/chat/completions"),
            "OpenCode Go chat URL must use zen/go/v1");
    Require(modelsUrl(AgentProviderKind::OpenCodeGo, QString())
                == QStringLiteral("https://opencode.ai/zen/go/v1/models"),
            "OpenCode Go models URL must be GET /v1/models");
    Require(modelsUrl(AgentProviderKind::OpenRouter, QString())
                .startsWith(QStringLiteral("https://openrouter.ai/api/v1/models")),
            "OpenRouter models URL must be /api/v1/models");
    Require(modelsUrl(AgentProviderKind::OpenRouter, QString()).contains(QStringLiteral("supported_parameters=tools")),
            "OpenRouter catalog fetch should ask the server for tool-capable models");
    Require(inferProviderKind(QStringLiteral("https://openrouter.ai/api/v1/chat/completions"))
                == AgentProviderKind::OpenRouter,
            "openrouter.ai must infer OpenRouter");
    Require(inferProviderKind(QStringLiteral("https://opencode.ai/zen/go/v1/chat/completions"))
                == AgentProviderKind::OpenCodeGo,
            "opencode.ai/zen/go must infer OpenCode Go");
    Require(reasoningProtocolFor(AgentProviderKind::DeepSeek, QString()) == ReasoningProtocol::DeepSeek,
            "DeepSeek must use thinking/reasoning_effort");
    Require(reasoningProtocolFor(AgentProviderKind::OpenRouter, QString()) == ReasoningProtocol::OpenRouter,
            "OpenRouter must use the reasoning object");
    Require(reasoningProtocolFor(AgentProviderKind::OpenCodeGo, QString()) == ReasoningProtocol::None,
            "OpenCode Go must not send DeepSeek thinking fields");

    const AgentProviderReadiness ready = providerReadiness(
        AgentProviderKind::Custom,
        QStringLiteral("https://user:password@[2001:db8::1]:8443/private/chat/completions?token=secret"),
        true,
        QStringLiteral("local-model"));
    Require(ready.isConfigured()
                && ready.endpointHost == QStringLiteral("[2001:db8::1]:8443")
                && ready.model == QStringLiteral("local-model"),
            "provider readiness must expose only the safe endpoint host and configured model");
    Require(!ready.endpointHost.contains(QStringLiteral("password"))
                && !ready.endpointHost.contains(QStringLiteral("private"))
                && !ready.endpointHost.contains(QStringLiteral("secret")),
            "provider readiness must not expose URL credentials, paths, or query strings");
    const AgentProviderReadiness missing_key = providerReadiness(
        AgentProviderKind::DeepSeek,
        chatCompletionsUrl(AgentProviderKind::DeepSeek, QString()), false,
        QStringLiteral("deepseek-chat"));
    Require(missing_key.issue == AgentProviderSetupIssue::ApiKey,
            "provider readiness must identify a missing API key without testing the network");
    const AgentProviderReadiness invalid_endpoint = providerReadiness(
        AgentProviderKind::Custom, QStringLiteral("not-a-network-url"), true,
        QStringLiteral("model"));
    Require(invalid_endpoint.issue == AgentProviderSetupIssue::Endpoint
                && invalid_endpoint.endpointHost.isEmpty(),
            "provider readiness must reject invalid endpoints without echoing them");
    const AgentProviderReadiness missing_model = providerReadiness(
        AgentProviderKind::Custom, QStringLiteral("http://127.0.0.1:11434/v1/chat/completions"),
        true, QString());
    Require(missing_model.issue == AgentProviderSetupIssue::Model,
            "provider readiness must identify a missing model");

    const QString verified_fingerprint = providerConfigurationFingerprint(
        AgentProviderKind::DeepSeek,
        QStringLiteral("https://api.deepseek.com"),
        QStringLiteral("sk-fingerprint-secret"),
        QStringLiteral("deepseek-chat"));
    Require(verified_fingerprint.size() == 64
                && !verified_fingerprint.contains(QStringLiteral("sk-fingerprint-secret")),
            "provider verification must persist a one-way SHA-256 fingerprint, not the API key");
    Require(verified_fingerprint == providerConfigurationFingerprint(
                AgentProviderKind::DeepSeek,
                QStringLiteral("https://api.deepseek.com/chat/completions/"),
                QStringLiteral("sk-fingerprint-secret"),
                QStringLiteral("deepseek-chat")),
            "equivalent configured endpoint forms must have one verification fingerprint");
    Require(verified_fingerprint != providerConfigurationFingerprint(
                AgentProviderKind::DeepSeek,
                QStringLiteral("https://api.deepseek.com"),
                QStringLiteral("sk-different-secret"),
                QStringLiteral("deepseek-chat"))
                && verified_fingerprint != providerConfigurationFingerprint(
                    AgentProviderKind::DeepSeek,
                    QStringLiteral("https://api.deepseek.com"),
                    QStringLiteral("sk-fingerprint-secret"),
                    QStringLiteral("deepseek-reasoner"))
                && verified_fingerprint != providerConfigurationFingerprint(
                    AgentProviderKind::OpenRouter,
                    QStringLiteral("https://api.deepseek.com"),
                    QStringLiteral("sk-fingerprint-secret"),
                    QStringLiteral("deepseek-chat")),
            "provider, API key, and model changes must invalidate connection verification");

    const CatalogResult deepseek = AgentModelCatalog::parseModelsJson(QByteArray(
        R"({"object":"list","data":[{"id":"deepseek-chat","object":"model"},{"id":"deepseek-reasoner"}]})"));
    Require(deepseek.error.isEmpty() && deepseek.models.size() == 2, "DeepSeek /models list must parse");
    Require(deepseek.models.first().id == QStringLiteral("deepseek-chat"), "DeepSeek model id must be preserved");

    CatalogResult openrouter = AgentModelCatalog::parseModelsJson(QByteArray(R"({
      "data": [{
        "id": "deepseek/deepseek-chat",
        "name": "DeepSeek Chat",
        "context_length": 64000,
        "supported_parameters": ["tools", "reasoning", "temperature"]
      }]
    })"));
    Require(openrouter.error.isEmpty() && openrouter.models.size() == 1, "OpenRouter /models list must parse");
    Require(openrouter.models.first().tools && openrouter.models.first().reasoning,
            "OpenRouter supported_parameters must map to tools and reasoning");
    Require(openrouter.models.first().contextLength == 64000, "OpenRouter context_length must be kept");
    Require(openrouter.models.first().supportedParameters.contains(QStringLiteral("tools")),
            "raw supported_parameters must be retained");

    const CatalogResult opencode = AgentModelCatalog::parseModelsJson(QByteArray(
        R"({"models":[{"id":"glm-5.1","name":"GLM-5.1","context_length":202800}]})"));
    Require(opencode.error.isEmpty() && opencode.models.first().id == QStringLiteral("glm-5.1"),
            "OpenCode-style models array must parse");

    AgentModelCatalog::applyProviderDefaults(&openrouter, AgentProviderKind::OpenRouter);
    CatalogResult cached = AgentModelCatalog::fromCacheJson(
        AgentModelCatalog::toCacheJson(openrouter, AgentProviderKind::OpenRouter));
    Require(cached.models.size() == 1 && cached.models.first().id == QStringLiteral("deepseek/deepseek-chat"),
            "catalog cache must round-trip model ids");

    ModelRequest request;
    request.model = QStringLiteral("deepseek-chat");
    request.thinking = true;
    request.reasoningEffort = QStringLiteral("medium");
    request.reasoningProtocol = ReasoningProtocol::DeepSeek;
    QJsonObject deepseek_body = OpenAICompatibleProvider::buildChatBody(request);
    Require(deepseek_body.value(QStringLiteral("thinking")).toObject().value(QStringLiteral("type")).toString()
                == QStringLiteral("enabled"),
            "DeepSeek body must send thinking");
    Require(deepseek_body.contains(QStringLiteral("reasoning_effort")),
            "DeepSeek body must send reasoning_effort");
    Require(!deepseek_body.contains(QStringLiteral("reasoning")),
            "DeepSeek body must not send the OpenRouter reasoning object");
    Require(deepseek_body.value(QStringLiteral("stream_options")).toObject()
                .value(QStringLiteral("include_usage")).toBool(),
            "streamed requests must ask compatible providers to report exact usage");

    request.reasoningProtocol = ReasoningProtocol::OpenRouter;
    QJsonObject openrouter_body = OpenAICompatibleProvider::buildChatBody(request);
    Require(!openrouter_body.contains(QStringLiteral("thinking")),
            "OpenRouter body must not send DeepSeek thinking");
    Require(openrouter_body.value(QStringLiteral("reasoning")).toObject().value(QStringLiteral("effort")).toString()
                == QStringLiteral("medium"),
            "OpenRouter body must send reasoning.effort");

    request.reasoningProtocol = ReasoningProtocol::None;
    request.maxOutputTokens = 8;
    QJsonObject plain_body = OpenAICompatibleProvider::buildChatBody(request);
    Require(!plain_body.contains(QStringLiteral("thinking")) && !plain_body.contains(QStringLiteral("reasoning")),
            "OpenCode Go / custom body must omit provider-specific reasoning fields");
    Require(plain_body.value(QStringLiteral("max_tokens")).toInt() == 8,
            "bounded requests must publish max_tokens");
    request.includeUsage = false;
    Require(!OpenAICompatibleProvider::buildChatBody(request)
                 .contains(QStringLiteral("stream_options")),
            "usage negotiation must be removable for endpoints that reject stream_options");

    AgentSession session;
    session.append(AgentEventType::UserMessage, QJsonObject {
        { QStringLiteral("text"), QStringLiteral("Summarize this book. key=sk-secretvalue999") }
    });
    session.append(AgentEventType::AssistantDelta, QJsonObject {
        { QStringLiteral("kind"), QStringLiteral("reasoning") },
        { QStringLiteral("text"), QStringLiteral("Inspect the spine.") }
    });
    session.append(AgentEventType::AssistantMessage, QJsonObject {
        { QStringLiteral("content"), QStringLiteral("Two chapters.") },
        { QStringLiteral("reasoning_content"), QStringLiteral("Inspect the spine.") }
    });
    session.append(AgentEventType::ToolCompleted, QJsonObject {
        { QStringLiteral("name"), QStringLiteral("book.summary") },
        { QStringLiteral("data"), QJsonObject { { QStringLiteral("title"), QStringLiteral("Junior Physics") } } }
    });
    const QJsonObject exported_plan {
        { QStringLiteral("plan_kind"), QStringLiteral("paragraph_normalization") },
        { QStringLiteral("plan_id"), QStringLiteral("plan-secret-binding") },
        { QStringLiteral("plan_digest"), QStringLiteral("digest-secret-binding") },
        { QStringLiteral("summary"), QJsonObject {
            { QStringLiteral("ready_files"), 1 },
            { QStringLiteral("conversion_count"), 2 },
            { QStringLiteral("protected_count"), 1 }
        } },
        { QStringLiteral("operation_groups_independent"), true },
        { QStringLiteral("operation_groups"), QJsonArray { QJsonObject {
            { QStringLiteral("group_id"), QStringLiteral("chapter-1") },
            { QStringLiteral("label"), QStringLiteral("Text/chapter-1.xhtml") },
            { QStringLiteral("resource_ids"), QJsonArray {
                QStringLiteral("chapter-1") } },
            { QStringLiteral("independently_applicable"), true }
        } } },
        { QStringLiteral("changes"), QJsonArray { QJsonObject {
            { QStringLiteral("resource_id"), QStringLiteral("chapter-1") },
            { QStringLiteral("book_path"), QStringLiteral("Text/chapter-1.xhtml") },
            { QStringLiteral("conversion_count"), 2 },
            { QStringLiteral("protected_count"), 1 },
            { QStringLiteral("source_diff"), QJsonObject {
                { QStringLiteral("before"), QStringLiteral("<div>Before</div>") },
                { QStringLiteral("after"), QStringLiteral("<p>After</p>") }
            } }
        } } },
        { QStringLiteral("local_validation"), QStringLiteral("passed") },
        { QStringLiteral("full_epubcheck"), QJsonObject {
            { QStringLiteral("status"), QStringLiteral("not_run") }
        } }
    };
    session.append(AgentEventType::ToolCompleted, QJsonObject {
        { QStringLiteral("name"), QStringLiteral("paragraphs.plan") },
        { QStringLiteral("data"), exported_plan }
    });
    session.append(AgentEventType::PlanCreated, exported_plan);
    const QJsonObject exported_toc_plan {
        { QStringLiteral("plan_kind"), QStringLiteral("toc_hierarchy") },
        { QStringLiteral("plan_id"), QStringLiteral("toc-plan-secret-binding") },
        { QStringLiteral("plan_digest"), QStringLiteral("toc-digest-secret-binding") },
        { QStringLiteral("affected_count"), 2 },
        { QStringLiteral("adopted_count"), 1 },
        { QStringLiteral("changes"), QJsonArray { QJsonObject {
            { QStringLiteral("label"), QStringLiteral("Chapter B") },
            { QStringLiteral("target"), QStringLiteral("Text/chapter-b.xhtml#start") },
            { QStringLiteral("from_depth"), 2 },
            { QStringLiteral("to_depth"), 1 },
            { QStringLiteral("from_parent_id"), 1 },
            { QStringLiteral("to_parent_id"), 0 }
        } } },
        { QStringLiteral("changes_truncated"), true },
        { QStringLiteral("local_validation"), QStringLiteral("passed") },
        { QStringLiteral("full_epubcheck"), QJsonObject {
            { QStringLiteral("status"), QStringLiteral("not_run") }
        } }
    };
    session.append(AgentEventType::ToolCompleted, QJsonObject {
        { QStringLiteral("name"), QStringLiteral("toc.plan_transform") },
        { QStringLiteral("data"), exported_toc_plan }
    });
    session.append(AgentEventType::PlanCreated, exported_toc_plan);
    session.append(AgentEventType::TransactionPreviewed, QJsonObject {
        { QStringLiteral("applied_to_book"), false },
        { QStringLiteral("save_status"), QStringLiteral("not_applied") }
    });
    session.append(AgentEventType::TransactionCommitted, QJsonObject {
        { QStringLiteral("applied_to_book"), true },
        { QStringLiteral("save_status"), QStringLiteral("not_saved") },
        { QStringLiteral("applied_changes"), 2 },
        { QStringLiteral("book_revision"), 9 },
        { QStringLiteral("full_epubcheck"), QJsonObject {
            { QStringLiteral("status"), QStringLiteral("not_run") }
        } },
        { QStringLiteral("recovery"), QJsonObject {
            { QStringLiteral("task_restore_point"), QStringLiteral("available") },
            { QStringLiteral("affected_resources"), QJsonArray {
                QStringLiteral("chapter-1"), QStringLiteral("chapter-2") } }
        } }
    });
    session.append(AgentEventType::TaskRestoreFailed, QJsonObject {
        { QStringLiteral("code"), QStringLiteral("TASK_RESTORE_CONFLICT") }
    });
    session.append(AgentEventType::TaskRestoreCompleted, QJsonObject {
        { QStringLiteral("affected_resources"), QJsonArray {
            QStringLiteral("chapter-1"), QStringLiteral("chapter-2") } }
    });
    session.append(AgentEventType::TransactionRolledBack, QJsonObject {
        { QStringLiteral("rolled_back"), true },
        { QStringLiteral("live_book_unchanged"), true }
    });

    SessionExportContext context;
    context.sessionId = session.id();
    context.provider = QStringLiteral("deepseek");
    context.model = QStringLiteral("deepseek-chat");
    context.mode = QStringLiteral("ask");
    context.secrets.append(QStringLiteral("sk-secretvalue999"));
    const QString markdown = exportConversationMarkdown(session, context);
    Require(markdown.contains(QStringLiteral("## You")), "conversation export must include the user turn");
    Require(markdown.contains(QStringLiteral("Two chapters.")), "conversation export must include the answer");
    Require(markdown.contains(QStringLiteral("book.summary")), "conversation export must include tools");
    Require(markdown.contains(QStringLiteral("## Plan review: Paragraph normalization"))
                && markdown.contains(QStringLiteral("Text/chapter-1.xhtml"))
                && markdown.contains(
                    QStringLiteral("Independent XHTML operation groups: 1"))
                && markdown.contains(QStringLiteral("<div>Before</div>"))
                && markdown.contains(QStringLiteral("<p>After</p>"))
                && markdown.contains(QStringLiteral("Full EPUBCheck: not run"))
                && !markdown.contains(QStringLiteral("plan-secret-binding"))
                && !markdown.contains(QStringLiteral("digest-secret-binding")),
            "conversation export must retain reviewable source changes without protocol bindings");
    Require(markdown.contains(QStringLiteral("## Plan review: TOC hierarchy"))
                && markdown.contains(QStringLiteral("Chapter B"))
                && markdown.contains(QStringLiteral("depth 2 → 1"))
                && markdown.contains(QStringLiteral("Additional TOC changes"))
                && !markdown.contains(QStringLiteral("toc-plan-secret-binding"))
                && !markdown.contains(QStringLiteral("toc-digest-secret-binding")),
            "conversation export must summarize bounded TOC reparenting without protocol bindings");
    Require(markdown.contains(QStringLiteral("Staged only. The live book was unchanged.")),
            "conversation export must distinguish preview from live-book changes");
    Require(markdown.contains(QStringLiteral("EPUB file has not been saved"))
                && markdown.contains(QStringLiteral("Applied changes: 2"))
                && markdown.contains(QStringLiteral("Book revision: 9")),
            "conversation export must preserve commit save state and counts");
    Require(markdown.contains(QStringLiteral("Full EPUBCheck: not run"))
                && markdown.contains(QStringLiteral("restore point is available for 2 text resource")),
            "conversation export must state validation and recovery boundaries");
    Require(markdown.contains(QStringLiteral("## Restore blocked"))
                && markdown.contains(QStringLiteral("No book content was changed"))
                && markdown.contains(QStringLiteral("## Task restored"))
                && markdown.contains(QStringLiteral("Restored 2 text resource")),
            "conversation export must preserve task recovery outcomes");
    Require(markdown.contains(QStringLiteral("## Staged changes discarded"))
                && markdown.contains(QStringLiteral("live book was not changed")),
            "conversation export must include rollback status");
    Require(!markdown.contains(QStringLiteral("sk-secretvalue999")),
            "conversation export must redact secrets");

    context.httpTraces.append(QJsonObject {
        { QStringLiteral("url"), QStringLiteral("https://api.deepseek.com/chat/completions") },
        { QStringLiteral("authorization"), QStringLiteral("Bearer sk-secretvalue999") },
        { QStringLiteral("request_body"), QStringLiteral("{\"model\":\"deepseek-chat\"}") }
    });
    const QByteArray debug = exportDebugJson(session, context);
    Require(!debug.contains("sk-secretvalue999"), "debug log must redact API keys");
    Require(QString::fromUtf8(debug).contains(QStringLiteral("sigil-native-agent-debug-v1")),
            "debug log must declare its format");
    Require(QString::fromUtf8(debug).contains(QStringLiteral("user_message")),
            "debug log must include session events");
    Require(QString::fromUtf8(debug).contains(QStringLiteral("plan_created"))
                && QString::fromUtf8(debug).contains(QStringLiteral("plan-secret-binding"))
                && QString::fromUtf8(debug).contains(QStringLiteral("toc-plan-secret-binding")),
            "debug log must retain the complete native plan event for diagnosis");
    Require(QString::fromUtf8(debug).contains(QStringLiteral("http_traces")),
            "debug log must include HTTP traces");
    const QJsonObject debug_object = QJsonDocument::fromJson(debug).object();
    Require(debug_object.value(QStringLiteral("assistant_delta_omitted")).toInt() >= 1,
            "debug log must report omitted per-token deltas");
    for (const QJsonValue &value : debug_object.value(QStringLiteral("events")).toArray()) {
        Require(value.toObject().value(QStringLiteral("type")).toString() != QStringLiteral("assistant_delta"),
                "debug events must not include per-token assistant_delta");
    }

    QTcpServer server;
    Require(server.listen(QHostAddress::LocalHost, 0), "local models server must listen");
    const QByteArray payload = QByteArray(R"({"data":[{"id":"fetched-model","name":"Fetched","context_length":8192,"supported_parameters":["tools"]}]})");
    QObject::connect(&server, &QTcpServer::newConnection, [&server, payload]() {
        while (server.hasPendingConnections()) {
            QTcpSocket *socket = server.nextPendingConnection();
            QObject::connect(socket, &QTcpSocket::readyRead, socket, [socket, payload]() {
                const QByteArray request = socket->peek(socket->bytesAvailable());
                if (!request.contains("\r\n\r\n") && !request.contains("\n\n")) return;
                socket->readAll();
                QByteArray response = "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: ";
                response += QByteArray::number(payload.size());
                response += "\r\nConnection: close\r\n\r\n";
                response += payload;
                socket->write(response);
                socket->flush();
                socket->disconnectFromHost();
            });
        }
    });

    const QString fetch_url = QStringLiteral("http://127.0.0.1:%1/models").arg(server.serverPort());
    QFutureWatcher<CatalogResult> catalog_watcher;
    QEventLoop catalog_loop;
    bool catalog_event_loop_responsive = false;
    QObject::connect(&catalog_watcher, &QFutureWatcher<CatalogResult>::finished,
                     &catalog_loop, &QEventLoop::quit);
    QTimer::singleShot(0, [&catalog_event_loop_responsive]() {
        catalog_event_loop_responsive = true;
    });
    catalog_watcher.setFuture(QtConcurrent::run([fetch_url]() {
        return AgentModelCatalog::fetch(
            fetch_url, QStringLiteral("sk-test-not-a-secret"),
            QString(), QString(), 5000);
    }));
    catalog_loop.exec();
    const CatalogResult fetched = catalog_watcher.result();
    Require(fetched.error.isEmpty(), "GET /models against a local server must succeed");
    Require(fetched.httpStatus == 200, "GET /models must surface HTTP 200");
    Require(fetched.models.size() == 1 && fetched.models.first().id == QStringLiteral("fetched-model"),
            "GET /models must parse the live payload");
    Require(fetched.models.first().tools && catalog_event_loop_responsive,
            "background model fetch must preserve parameters while the main event loop stays responsive");
    Require(!fetched.sourceUrl.contains(QStringLiteral("sk-")), "catalog result must not echo the API key");

    QTcpServer probe_server;
    Require(probe_server.listen(QHostAddress::LocalHost, 0),
            "local Chat Completions probe server must listen");
    QByteArray probe_request;
    const QByteArray probe_payload = QByteArray(
        "data: {\"choices\":[{\"delta\":{\"content\":\"OK\"},\"finish_reason\":\"stop\"}]}\n\n"
        "data: [DONE]\n\n");
    QObject::connect(&probe_server, &QTcpServer::newConnection,
                     [&probe_server, &probe_request, probe_payload]() {
        while (probe_server.hasPendingConnections()) {
            QTcpSocket *socket = probe_server.nextPendingConnection();
            QObject::connect(socket, &QTcpSocket::readyRead, socket,
                             [socket, &probe_request, probe_payload]() {
                probe_request += socket->readAll();
                const int header_end = probe_request.indexOf("\r\n\r\n");
                if (header_end < 0) return;
                const QRegularExpression content_length(
                    QStringLiteral("Content-Length: \\s*(\\d+)"),
                    QRegularExpression::CaseInsensitiveOption);
                const QRegularExpressionMatch match = content_length.match(
                    QString::fromLatin1(probe_request.left(header_end)));
                if (!match.hasMatch()) return;
                const int body_size = match.captured(1).toInt();
                if (probe_request.size() < header_end + 4 + body_size) return;
                QByteArray response =
                    "HTTP/1.1 200 OK\r\nContent-Type: text/event-stream\r\nContent-Length: ";
                response += QByteArray::number(probe_payload.size());
                response += "\r\nConnection: close\r\n\r\n";
                response += probe_payload;
                socket->write(response);
                socket->flush();
                socket->disconnectFromHost();
            });
        }
    });

    OpenAIProviderConfig probe_config;
    probe_config.baseUrl = QStringLiteral("http://127.0.0.1:%1/chat/completions")
                               .arg(probe_server.serverPort());
    probe_config.apiKey = QStringLiteral("sk-probe-test-secret");
    probe_config.model = QStringLiteral("probe-model");
    probe_config.thinking = true;
    probe_config.reasoningProtocol = ReasoningProtocol::DeepSeek;
    QFutureWatcher<AgentConnectionProbeResult> probe_watcher;
    QEventLoop probe_loop;
    bool main_event_loop_responsive = false;
    QObject::connect(&probe_watcher,
                     &QFutureWatcher<AgentConnectionProbeResult>::finished,
                     &probe_loop, &QEventLoop::quit);
    QTimer::singleShot(0, [&main_event_loop_responsive]() {
        main_event_loop_responsive = true;
    });
    probe_watcher.setFuture(QtConcurrent::run([probe_config]() {
        return probeAgentConnection(probe_config, 2000);
    }));
    probe_loop.exec();
    const AgentConnectionProbeResult probe = probe_watcher.result();
    Require(probe.ok && probe.httpStatus == 200 && probe.model == probe_config.model
                && probe.finishReason == QStringLiteral("stop")
                && probe.durationMs >= 0 && main_event_loop_responsive,
            "a background Chat Completions probe must pass while the main event loop stays responsive");
    const int probe_header_end = probe_request.indexOf("\r\n\r\n");
    const QJsonObject probe_body = QJsonDocument::fromJson(
        probe_request.mid(probe_header_end + 4)).object();
    Require(probe_request.startsWith("POST /chat/completions ")
                && probe_request.contains("Authorization: Bearer sk-probe-test-secret")
                && probe_body.value(QStringLiteral("model")).toString()
                    == QStringLiteral("probe-model")
                && probe_body.value(QStringLiteral("stream")).toBool()
                && probe_body.value(QStringLiteral("max_tokens")).toInt() == 8
                && probe_body.value(QStringLiteral("thinking")).toObject()
                    .value(QStringLiteral("type")).toString() == QStringLiteral("disabled")
                && !probe_body.contains(QStringLiteral("stream_options"))
                && !probe_body.contains(QStringLiteral("tools")),
            "connection probe must be a tiny no-tools request without optional extensions");

    QTcpServer timing_server;
    Require(timing_server.listen(QHostAddress::LocalHost, 0),
            "local response timing server must listen");
    QByteArray timing_http_request;
    QObject::connect(&timing_server, &QTcpServer::newConnection,
                     [&timing_server, &timing_http_request]() {
        while (timing_server.hasPendingConnections()) {
            QTcpSocket *socket = timing_server.nextPendingConnection();
            QObject::connect(socket, &QTcpSocket::readyRead, socket,
                             [socket, &timing_http_request]() {
                timing_http_request += socket->readAll();
                if (socket->property("responseScheduled").toBool()) return;
                const int header_end = timing_http_request.indexOf("\r\n\r\n");
                if (header_end < 0) return;
                const QRegularExpression content_length(
                    QStringLiteral("Content-Length: \\s*(\\d+)"),
                    QRegularExpression::CaseInsensitiveOption);
                const QRegularExpressionMatch match = content_length.match(
                    QString::fromLatin1(timing_http_request.left(header_end)));
                if (!match.hasMatch()) return;
                const int body_size = match.captured(1).toInt();
                if (timing_http_request.size() < header_end + 4 + body_size) return;
                socket->setProperty("responseScheduled", true);
                QTimer::singleShot(30, socket, [socket]() {
                    socket->write(
                        "HTTP/1.1 200 OK\r\n"
                        "Content-Type: text/event-stream\r\n"
                        "Connection: close\r\n\r\n"
                        ": keepalive\n\n");
                    socket->flush();
                });
                QTimer::singleShot(100, socket, [socket]() {
                    socket->write(
                        "data: {\"choices\":[{\"delta\":{\"content\":\"OK\"},"
                        "\"finish_reason\":\"stop\"}]}");
                    socket->flush();
                    socket->disconnectFromHost();
                });
            });
        }
    });

    OpenAIProviderConfig timing_config;
    timing_config.baseUrl = QStringLiteral("http://127.0.0.1:%1/chat/completions")
                                .arg(timing_server.serverPort());
    timing_config.apiKey = QStringLiteral("sk-timing-test-secret");
    timing_config.model = QStringLiteral("timing-model");
    timing_config.reasoningProtocol = ReasoningProtocol::None;
    timing_config.requestUsage = false;
    OpenAICompatibleProvider timing_provider(timing_config);
    ModelRequest timing_model_request;
    timing_model_request.model = timing_config.model;
    timing_model_request.includeUsage = false;
    timing_model_request.timeoutMs = 2000;
    ChatMessage timing_user;
    timing_user.role = QStringLiteral("user");
    timing_user.content = QStringLiteral("hello");
    timing_model_request.messages.append(timing_user);
    RecordingSink timing_sink;
    const ModelTurn timed_turn =
        timing_provider.stream(timing_model_request, timing_sink);
    Require(timed_turn.error.isEmpty()
                && timed_turn.content == QStringLiteral("OK")
                && timing_sink.content == QStringLiteral("OK"),
            "a final SSE event without a trailing newline must reach both the turn and sink");
    Require(timed_turn.timing.firstByteMs >= 15
                && timed_turn.timing.firstByteMs < 1000
                && timed_turn.timing.firstEventMs >= 70
                && timed_turn.timing.firstEventMs < 1500
                && timed_turn.timing.firstEventMs > timed_turn.timing.firstByteMs,
            "response timing must distinguish an early keepalive byte from the first model event");
    const QJsonArray timing_traces = timing_provider.debugTraces();
    const QJsonObject timing_trace = timing_traces.isEmpty()
        ? QJsonObject() : timing_traces.at(timing_traces.size() - 1).toObject();
    Require(timing_trace.value(QStringLiteral("first_byte_ms")).toInteger()
                    == timed_turn.timing.firstByteMs
                && timing_trace.value(QStringLiteral("first_model_event_ms")).toInteger()
                    == timed_turn.timing.firstEventMs,
            "HTTP traces must retain the same safe response latency breakdown");

    QTcpServer error_server;
    Require(error_server.listen(QHostAddress::LocalHost, 0),
            "local provider error server must listen");
    const QString provider_key = QStringLiteral("sk-provider-error-secret");
    const QByteArray error_payload = QStringLiteral(
        R"({"error":{"message":"credential %1 was rejected"}})")
        .arg(provider_key).toUtf8();
    QObject::connect(&error_server, &QTcpServer::newConnection,
                     [&error_server, error_payload]() {
        while (error_server.hasPendingConnections()) {
            QTcpSocket *socket = error_server.nextPendingConnection();
            QObject::connect(socket, &QTcpSocket::readyRead, socket,
                             [socket, error_payload]() {
                const QByteArray request = socket->peek(socket->bytesAvailable());
                if (!request.contains("\r\n\r\n") && !request.contains("\n\n")) return;
                socket->readAll();
                QByteArray response =
                    "HTTP/1.1 401 Unauthorized\r\nContent-Type: application/json\r\nContent-Length: ";
                response += QByteArray::number(error_payload.size());
                response += "\r\nConnection: close\r\n\r\n";
                response += error_payload;
                socket->write(response);
                socket->flush();
                socket->disconnectFromHost();
            });
        }
    });

    OpenAIProviderConfig error_config;
    error_config.baseUrl = QStringLiteral("http://127.0.0.1:%1/chat/completions")
                               .arg(error_server.serverPort());
    error_config.apiKey = provider_key;
    error_config.model = QStringLiteral("test-model");
    OpenAICompatibleProvider error_provider(error_config);
    ModelRequest error_request;
    error_request.model = error_config.model;
    ChatMessage error_user;
    error_user.role = QStringLiteral("user");
    error_user.content = QStringLiteral("hello");
    error_request.messages.append(error_user);
    NullSink null_sink;
    const ModelTurn provider_error = error_provider.stream(error_request, null_sink);
    Require(provider_error.error.contains(QStringLiteral("HTTP 401"))
                && provider_error.error.contains(QStringLiteral("[redacted]"))
                && !provider_error.error.contains(provider_key),
            "provider errors must remain readable while redacting an echoed API key");
    const QByteArray trace_json = QJsonDocument(error_provider.debugTraces()).toJson();
    Require(!trace_json.contains(provider_key.toUtf8())
                && trace_json.contains("[redacted]"),
            "in-memory HTTP traces must redact an echoed API key before export");
    const AgentConnectionProbeResult rejected_probe =
        probeAgentConnection(error_config, 2000);
    Require(!rejected_probe.ok && rejected_probe.httpStatus == 401
                && rejected_probe.error.contains(QStringLiteral("[redacted]"))
                && !rejected_probe.error.contains(provider_key),
            "connection probe must surface authentication failure without echoing the key");
    const CatalogResult rejected_catalog = AgentModelCatalog::fetch(
        error_config.baseUrl, provider_key, QString(), QString(), 2000);
    Require(!rejected_catalog.error.isEmpty()
                && rejected_catalog.error.contains(QStringLiteral("[redacted]"))
                && !rejected_catalog.error.contains(provider_key)
                && !rejected_catalog.sourceUrl.contains(provider_key),
            "model catalog errors and result metadata must redact the configured API key");

    QTcpServer timeout_server;
    Require(timeout_server.listen(QHostAddress::LocalHost, 0),
            "local timeout probe server must listen");
    QObject::connect(&timeout_server, &QTcpServer::newConnection, [&timeout_server]() {
        while (timeout_server.hasPendingConnections()) {
            QTcpSocket *socket = timeout_server.nextPendingConnection();
            QObject::connect(socket, &QTcpSocket::readyRead, socket, [socket]() {
                socket->readAll();
            });
        }
    });
    OpenAIProviderConfig timeout_config = probe_config;
    timeout_config.baseUrl = QStringLiteral("http://127.0.0.1:%1/chat/completions")
                                 .arg(timeout_server.serverPort());
    const AgentConnectionProbeResult timed_out =
        probeAgentConnection(timeout_config, 120);
    Require(!timed_out.ok && timed_out.httpStatus == 0
                && timed_out.error.contains(QStringLiteral("timed out after 120 ms"))
                && timed_out.durationMs >= 100 && timed_out.durationMs < 2000,
            "connection probe timeout must be explicit, bounded, and never reported as success");

    std::atomic_bool cancel_probe { false };
    QTimer::singleShot(20, [&cancel_probe]() {
        cancel_probe.store(true, std::memory_order_relaxed);
    });
    QElapsedTimer cancel_timer;
    cancel_timer.start();
    const AgentConnectionProbeResult cancelled_probe =
        probeAgentConnection(timeout_config, 5000, &cancel_probe);
    Require(!cancelled_probe.ok
                && cancelled_probe.error == QStringLiteral("cancelled")
                && cancel_timer.elapsed() < 1000,
            "connection probes must observe external cancellation without waiting for timeout");

    std::atomic_bool cancel_catalog { false };
    QTimer::singleShot(20, [&cancel_catalog]() {
        cancel_catalog.store(true, std::memory_order_relaxed);
    });
    QElapsedTimer catalog_cancel_timer;
    catalog_cancel_timer.start();
    const CatalogResult cancelled_catalog = AgentModelCatalog::fetch(
        timeout_config.baseUrl, timeout_config.apiKey,
        QString(), QString(), 5000, &cancel_catalog);
    Require(cancelled_catalog.error == QStringLiteral("Models request cancelled")
                && catalog_cancel_timer.elapsed() < 1000,
            "model catalog fetch must observe external cancellation without waiting for timeout");

    return EXIT_SUCCESS;
}
