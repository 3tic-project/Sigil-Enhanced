/************************************************************************
**
**  This file is part of Sigil-Enhanced.
**
*************************************************************************/

#include "Agent/Model/OpenAICompatibleProvider.h"

#include <QElapsedTimer>
#include <QEventLoop>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimer>

#include "Agent/Model/HistoryAssembler.h"
#include "Agent/Model/StreamingJsonDecoder.h"

namespace SigilAgent
{

namespace
{

QString clipUtf8(const QByteArray &data, int max_bytes)
{
    if (data.size() <= max_bytes) return QString::fromUtf8(data);
    return QString::fromUtf8(data.left(max_bytes)) + QStringLiteral("…");
}

QJsonObject makeHttpTrace(const QString &url,
                          const QString &model,
                          const QByteArray &request,
                          const QByteArray &response,
                          int status,
                          qint64 elapsed_ms,
                          const QString &error)
{
    QJsonObject trace {
        { QStringLiteral("method"), QStringLiteral("POST") },
        { QStringLiteral("url"), url },
        { QStringLiteral("model"), model },
        { QStringLiteral("status"), status },
        { QStringLiteral("elapsed_ms"), elapsed_ms },
        { QStringLiteral("error"), error },
        { QStringLiteral("request_bytes"), request.size() },
        { QStringLiteral("response_bytes"), response.size() },
        { QStringLiteral("request_body"), clipUtf8(request, 65536) },
        { QStringLiteral("response_head"), clipUtf8(response, 8192) }
    };
    if (response.size() > 8192) {
        trace.insert(QStringLiteral("response_tail"), QString::fromUtf8(response.right(8192)));
        trace.insert(QStringLiteral("response_truncated"), true);
    } else {
        trace.insert(QStringLiteral("response_truncated"), false);
    }
    return trace;
}

} // namespace

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
    caps.reasoning = m_config.reasoningProtocol != ReasoningProtocol::None;
    caps.toolCalling = true;
    return caps;
}

QJsonArray OpenAICompatibleProvider::debugTraces() const
{
    return m_traces;
}

void OpenAICompatibleProvider::recordTrace(const QJsonObject &trace)
{
    m_traces.append(trace);
    while (m_traces.size() > 32) m_traces.removeFirst();
}

QJsonObject OpenAICompatibleProvider::buildChatBody(const ModelRequest &request)
{
    HistoryAssembler assembler;
    QJsonObject body;
    body.insert(QStringLiteral("model"), request.model);
    body.insert(QStringLiteral("stream"), request.stream);
    body.insert(QStringLiteral("messages"),
                assembler.toOpenAIMessages(request.messages, !request.tools.isEmpty()));
    if (request.reasoningProtocol == ReasoningProtocol::DeepSeek) {
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
    } else if (request.reasoningProtocol == ReasoningProtocol::OpenRouter && request.thinking) {
        QJsonObject reasoning;
        if (!request.reasoningEffort.isEmpty()) {
            reasoning.insert(QStringLiteral("effort"), request.reasoningEffort);
        }
        reasoning.insert(QStringLiteral("exclude"), false);
        body.insert(QStringLiteral("reasoning"), reasoning);
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
    if (outgoing.model.isEmpty()) {
        turn.error = QStringLiteral("Model is not configured. Choose one in Preferences → Native Agent.");
        return turn;
    }
    outgoing.thinking = m_config.thinking && request.thinking;
    if (outgoing.reasoningEffort.isEmpty()) outgoing.reasoningEffort = m_config.reasoningEffort;
    outgoing.reasoningProtocol = m_config.reasoningProtocol;

    const QJsonObject body = buildChatBody(outgoing);
    const QByteArray payload = QJsonDocument(body).toJson(QJsonDocument::Compact);

    QNetworkRequest http(QUrl(m_config.baseUrl));
    http.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    http.setRawHeader("Authorization", QByteArray("Bearer ") + m_config.apiKey.toUtf8());
    http.setRawHeader("Accept", "text/event-stream");
    if (!m_config.httpReferer.isEmpty()) {
        http.setRawHeader("HTTP-Referer", m_config.httpReferer.toUtf8());
    }
    if (!m_config.httpTitle.isEmpty()) {
        http.setRawHeader("X-Title", m_config.httpTitle.toUtf8());
    }

    QNetworkAccessManager manager;
    QNetworkReply *reply = manager.post(http, payload);
    StreamingJsonDecoder decoder;
    QByteArray raw;
    QElapsedTimer timer;
    timer.start();
    QEventLoop loop;
    QObject::connect(reply, &QNetworkReply::readyRead, reply, [reply, &decoder, &sink, &turn, &raw]() {
        if (sink.isCancelled()) {
            reply->abort();
            return;
        }
        const QByteArray chunk = reply->readAll();
        raw += chunk;
        decoder.feed(chunk);
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

    QTimer cancel_poll;
    cancel_poll.setInterval(50);
    QObject::connect(&cancel_poll, &QTimer::timeout, reply, [reply, &sink]() {
        if (sink.isCancelled()) reply->abort();
    });
    cancel_poll.start();
    if (sink.isCancelled()) reply->abort();
    loop.exec();
    cancel_poll.stop();
    timeout.stop();

    if (sink.isCancelled()) {
        turn.error = QStringLiteral("cancelled");
        turn.finishReason = QStringLiteral("cancelled");
        recordTrace(makeHttpTrace(m_config.baseUrl, outgoing.model, payload, raw,
                                  reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt(),
                                  timer.elapsed(), QStringLiteral("cancelled")));
        reply->deleteLater();
        return turn;
    }

    const QByteArray leftover = reply->readAll();
    if (!leftover.isEmpty()) {
        raw += leftover;
        decoder.feed(leftover);
        if (turn.error.isEmpty() && !decoder.error().isEmpty()) turn.error = decoder.error();
    }
    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    if (reply->error() != QNetworkReply::NoError
        && reply->error() != QNetworkReply::OperationCanceledError
        && turn.error.isEmpty()) {
        QString snippet = QString::fromUtf8(raw.left(800)).simplified();
        QJsonParseError parse_error;
        const QJsonDocument document = QJsonDocument::fromJson(raw, &parse_error);
        if (parse_error.error == QJsonParseError::NoError) {
            const QString message = document.object().value(QStringLiteral("error")).toObject()
                                        .value(QStringLiteral("message")).toString();
            if (!message.isEmpty()) snippet = message;
        }
        turn.error = status > 0
            ? QStringLiteral("HTTP %1: %2").arg(status).arg(
                  snippet.isEmpty() ? reply->errorString() : snippet)
            : QStringLiteral("HTTP error: %1 %2").arg(reply->errorString(), snippet);
    }
    if (turn.error.isEmpty()) {
        turn = decoder.finish();
    }
    recordTrace(makeHttpTrace(m_config.baseUrl, outgoing.model, payload, raw,
                              status, timer.elapsed(), turn.error));
    reply->deleteLater();
    return turn;
}

} // namespace SigilAgent
