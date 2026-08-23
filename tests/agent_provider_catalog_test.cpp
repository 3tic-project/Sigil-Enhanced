#include <cstdlib>
#include <iostream>

#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QHostAddress>
#include <QJsonObject>
#include <QTcpServer>
#include <QTcpSocket>

#include "Agent/Core/AgentSession.h"
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
    QJsonObject plain_body = OpenAICompatibleProvider::buildChatBody(request);
    Require(!plain_body.contains(QStringLiteral("thinking")) && !plain_body.contains(QStringLiteral("reasoning")),
            "OpenCode Go / custom body must omit provider-specific reasoning fields");

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

    return EXIT_SUCCESS;
}
