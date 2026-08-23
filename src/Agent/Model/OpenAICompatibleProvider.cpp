/************************************************************************
**
**  This file is part of Sigil-Enhanced.
**
*************************************************************************/

#include "Agent/Model/OpenAICompatibleProvider.h"

#include <QEventLoop>
#include <QJsonArray>
#include <QJsonDocument>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimer>

#include "Agent/Model/HistoryAssembler.h"
#include "Agent/Model/StreamingJsonDecoder.h"

namespace SigilAgent
{

OpenAICompatibleProvider::OpenAICompatibleProvider(OpenAIProviderConfig config) :
    m_config(std::move(config))
{
}

void OpenAICompatibleProvider::setConfig(const OpenAIProviderConfig &config)
{
    m_config = config;
}

OpenAIProviderConfig OpenAICompatibleProvider::config() const
{
    return m_config;
}

ModelCapabilities OpenAICompatibleProvider::capabilities() const
{
    ModelCapabilities caps;
    caps.reasoning = true;
    caps.toolCalling = true;
    return caps;
}

QJsonObject OpenAICompatibleProvider::buildChatBody(const ModelRequest &request)
{
    HistoryAssembler assembler;
    QJsonObject body;
    body.insert(QStringLiteral("model"), request.model);
    body.insert(QStringLiteral("stream"), request.stream);
    body.insert(QStringLiteral("messages"),
                assembler.toOpenAIMessages(request.messages, !request.tools.isEmpty()));
    if (request.thinking) {
        body.insert(QStringLiteral("thinking"), QJsonObject {
            { QStringLiteral("type"), QStringLiteral("enabled") }
        });
    } else {
        body.insert(QStringLiteral("thinking"), QJsonObject {
            { QStringLiteral("type"), QStringLiteral("disabled") }
        });
    }
    if (!request.reasoningEffort.isEmpty()) {
        body.insert(QStringLiteral("reasoning_effort"), request.reasoningEffort);
    }
    if (!request.tools.isEmpty()) {
        body.insert(QStringLiteral("tools"), request.tools);
    }
    return body;
}

ModelTurn OpenAICompatibleProvider::stream(const ModelRequest &request, ModelStreamSink &sink)
{
    ModelTurn turn;
    if (m_config.baseUrl.isEmpty()) {
        turn.error = QStringLiteral("Provider base URL is not configured");
        return turn;
    }
    if (m_config.apiKey.isEmpty()) {
        turn.error = QStringLiteral("Provider API key is not configured");
        return turn;
    }

    ModelRequest outgoing = request;
    if (outgoing.model.isEmpty()) outgoing.model = m_config.model;
    outgoing.thinking = m_config.thinking && request.thinking;
    if (outgoing.reasoningEffort.isEmpty()) outgoing.reasoningEffort = m_config.reasoningEffort;

    const QJsonObject body = buildChatBody(outgoing);
    const QByteArray payload = QJsonDocument(body).toJson(QJsonDocument::Compact);

    QNetworkRequest http(QUrl(m_config.baseUrl));
    http.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    http.setRawHeader("Authorization", QByteArray("Bearer ") + m_config.apiKey.toUtf8());
    http.setRawHeader("Accept", "text/event-stream");

    QNetworkAccessManager manager;
    QNetworkReply *reply = manager.post(http, payload);
    StreamingJsonDecoder decoder;
    QEventLoop loop;
    QObject::connect(reply, &QNetworkReply::readyRead, reply, [reply, &decoder, &sink, &turn]() {
        if (sink.isCancelled()) {
            reply->abort();
            return;
        }
        decoder.feed(reply->readAll());
        const QList<StreamDelta> deltas = decoder.takeDeltas();
        for (const StreamDelta &delta : deltas) {
            if (!delta.reasoning.isEmpty()) sink.onReasoningDelta(delta.reasoning);
            if (!delta.content.isEmpty()) sink.onContentDelta(delta.content);
            if (!delta.toolCalls.isEmpty()) sink.onToolCallsUpdated(delta.toolCalls);
        }
        if (!decoder.error().isEmpty()) {
            turn.error = decoder.error();
            reply->abort();
        }
    });
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);

    QTimer timeout;
    timeout.setSingleShot(true);
    QObject::connect(&timeout, &QTimer::timeout, reply, [reply]() { reply->abort(); });
    timeout.start(120000);
    loop.exec();
    timeout.stop();

    if (sink.isCancelled()) {
        turn.error = QStringLiteral("cancelled");
        turn.finishReason = QStringLiteral("cancelled");
        reply->deleteLater();
        return turn;
    }

    if (reply->error() != QNetworkReply::NoError && turn.error.isEmpty()
        && reply->error() != QNetworkReply::OperationCanceledError) {
        const QByteArray err_body = reply->readAll();
        turn.error = QStringLiteral("HTTP error: %1").arg(reply->errorString());
        Q_UNUSED(err_body);
    }
    if (turn.error.isEmpty()) {
        decoder.feed(reply->readAll());
        turn = decoder.finish();
    }
    reply->deleteLater();
    return turn;
}

} // namespace SigilAgent
