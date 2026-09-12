#include <atomic>
#include <cstdlib>
#include <iostream>

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QJsonArray>
#include <QJsonDocument>
#include <QHostAddress>
#include <QJsonObject>
#include <QRegularExpression>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>

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
    CatalogResult fetched = AgentModelCatalog::fetch(fetch_url, QStringLiteral("sk-test-not-a-secret"),
                                                     QString(), QString(), 5000);
    Require(fetched.error.isEmpty(), "GET /models against a local server must succeed");
    Require(fetched.httpStatus == 200, "GET /models must surface HTTP 200");
    Require(fetched.models.size() == 1 && fetched.models.first().id == QStringLiteral("fetched-model"),
            "GET /models must parse the live payload");
    Require(fetched.models.first().tools, "fetched supported_parameters.tools must be recorded");
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
    const AgentConnectionProbeResult probe = probeAgentConnection(probe_config, 2000);
    Require(probe.ok && probe.httpStatus == 200 && probe.model == probe_config.model
                && probe.finishReason == QStringLiteral("stop")
                && probe.durationMs >= 0,
            "a valid streaming Chat Completions response must pass the connection probe");
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
                && !probe_body.contains(QStringLiteral("tools")),
            "connection probe must be a tiny no-tools request with thinking disabled");

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

    return EXIT_SUCCESS;
}
