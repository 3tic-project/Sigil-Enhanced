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
#include <QUuid>

#include "Agent/Model/HistoryAssembler.h"
#include "Agent/Model/AgentProviderPreset.h"
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

QString redactConfiguredSecret(QString text, const QString &secret)
{
    if (!secret.isEmpty()) text.replace(secret, QStringLiteral("[redacted]"));
    return text;
}

QJsonObject makeHttpTrace(const QString &url,
                          const QString &model,
                          const QByteArray &request,
                          const QByteArray &response,
                          int status,
                          qint64 elapsed_ms,
                          const QString &error,
                          const QString &secret,
                          const ModelResponseTiming &timing)
{
    QJsonObject trace {
        { QStringLiteral("method"), QStringLiteral("POST") },
        { QStringLiteral("url"), redactConfiguredSecret(url, secret) },
        { QStringLiteral("model"), model },
        { QStringLiteral("status"), status },
        { QStringLiteral("elapsed_ms"), elapsed_ms },
        { QStringLiteral("error"), redactConfiguredSecret(error, secret) },
        { QStringLiteral("request_bytes"), request.size() },
        { QStringLiteral("response_bytes"), response.size() },
        { QStringLiteral("request_body"),
          redactConfiguredSecret(clipUtf8(request, 65536), secret) },
        { QStringLiteral("response_head"),
          redactConfiguredSecret(clipUtf8(response, 8192), secret) }
    };
    if (response.size() > 8192) {
        trace.insert(QStringLiteral("response_tail"),
                     redactConfiguredSecret(QString::fromUtf8(response.right(8192)), secret));
        trace.insert(QStringLiteral("response_truncated"), true);
    } else {
        trace.insert(QStringLiteral("response_truncated"), false);
    }
    if (timing.firstByteMs >= 0) {
        trace.insert(QStringLiteral("first_byte_ms"), timing.firstByteMs);
    }
    if (timing.firstEventMs >= 0) {
        trace.insert(QStringLiteral("first_model_event_ms"), timing.firstEventMs);
    }
    return trace;
}

} // namespace

OpenAICompatibleProvider::OpenAICompatibleProvider(OpenAIProviderConfig config) :
    m_config(std::move(config)),
    m_fallbackSessionId(QUuid::createUuid().toString(QUuid::WithoutBraces))
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
    QList<ChatMessage> messages = request.messages;
    if (request.reasoningProtocol != ReasoningProtocol::OpenRouter) {
        for (ChatMessage &message : messages) {
            message.reasoningDetails = QJsonArray();
            message.hasReasoning = !message.reasoningContent.isEmpty();
        }
    }
    QJsonObject body;
    body.insert(QStringLiteral("model"), request.model);
    body.insert(QStringLiteral("stream"), request.stream);
    if (request.stream && request.includeUsage) {
        body.insert(QStringLiteral("stream_options"), QJsonObject {
            { QStringLiteral("include_usage"), true }
        });
    }
    body.insert(QStringLiteral("messages"),
                assembler.toOpenAIMessages(messages,
                    !request.tools.isEmpty() || request.reasoningProtocol == ReasoningProtocol::OpenRouter));
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
        } else {
            reasoning.insert(QStringLiteral("enabled"), true);
        }
        reasoning.insert(QStringLiteral("exclude"), false);
        body.insert(QStringLiteral("reasoning"), reasoning);
    }
    if (!request.tools.isEmpty()) {
        body.insert(QStringLiteral("tools"), request.tools);
    }
    if (request.maxOutputTokens > 0) {
        body.insert(QStringLiteral("max_tokens"), request.maxOutputTokens);
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
    if (m_config.openCodeGo) {
        if (outgoing.model.startsWith(QLatin1String("opencode-go/"), Qt::CaseInsensitive)) {
            outgoing.model.remove(0, QStringLiteral("opencode-go/").size());
        }
        const QString endpoint = openCodeGoEndpointForModel(outgoing.model);
        if (endpoint != QLatin1String("/chat/completions")) {
            turn.error = QStringLiteral("OpenCode Go model %1 requires %2; this client supports Chat Completions only")
                             .arg(outgoing.model, endpoint);
            return turn;
        }
    }
    outgoing.thinking = m_config.thinking && request.thinking;
    outgoing.includeUsage = m_config.requestUsage && request.includeUsage;
    if (outgoing.reasoningEffort.isEmpty()) outgoing.reasoningEffort = m_config.reasoningEffort;
    if (m_config.reasoningProtocol == ReasoningProtocol::OpenRouter
        && !m_config.reasoningEffortSelectable) {
        outgoing.reasoningEffort.clear();
    }
    if (m_config.reasoningProtocol == ReasoningProtocol::OpenRouter
        && !m_config.supportedReasoningEfforts.isEmpty()
        && !m_config.supportedReasoningEfforts.contains(outgoing.reasoningEffort,
                                                        Qt::CaseInsensitive)) {
        outgoing.reasoningEffort =
            m_config.defaultReasoningEffort.compare(QLatin1String("none"), Qt::CaseInsensitive) != 0
            && m_config.supportedReasoningEfforts.contains(
                m_config.defaultReasoningEffort, Qt::CaseInsensitive)
            ? m_config.defaultReasoningEffort : QString();
    }
    outgoing.reasoningProtocol = m_config.reasoningProtocol;

    const QJsonObject body = buildChatBody(outgoing);
    const QByteArray payload = QJsonDocument(body).toJson(QJsonDocument::Compact);

    QNetworkRequest http(QUrl(m_config.baseUrl));
    http.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    http.setRawHeader("Authorization", QByteArray("Bearer ") + m_config.apiKey.toUtf8());
    http.setRawHeader("Accept", "text/event-stream");
    if (m_config.openCodeGo) {
        http.setHeader(QNetworkRequest::UserAgentHeader, m_config.userAgent);
        const QString session_id = outgoing.sessionId.isEmpty()
            ? m_fallbackSessionId : outgoing.sessionId;
        http.setRawHeader("x-opencode-session", session_id.toUtf8());
    }
    if (!m_config.httpReferer.isEmpty()) {
        http.setRawHeader("HTTP-Referer", m_config.httpReferer.toUtf8());
    }
    if (!m_config.httpTitle.isEmpty()) {
        http.setRawHeader("X-OpenRouter-Title", m_config.httpTitle.toUtf8());
    }

    QElapsedTimer timer;
    timer.start();
    QNetworkAccessManager manager;
    QNetworkReply *reply = manager.post(http, payload);
    StreamingJsonDecoder decoder;
    QByteArray raw;
    qint64 first_byte_ms = -1;
    qint64 first_event_ms = -1;
    QTimer timeout;
    timeout.setSingleShot(true);
    const int first_token_timeout_ms = qBound(100, m_config.firstTokenTimeoutMs,
                                               MAX_FIRST_TOKEN_TIMEOUT_MS);
    const int idle_timeout_ms = qBound(100, outgoing.timeoutMs, 120000);
    bool saw_model_output = false;
    bool timed_out = false;
    bool timed_out_before_output = false;
    const auto publish_deltas = [&decoder, &sink, &timer, &first_event_ms,
                                 &timeout, &saw_model_output, idle_timeout_ms]() {
        const QList<StreamDelta> deltas = decoder.takeDeltas();
        for (const StreamDelta &delta : deltas) {
            const bool has_output = !delta.reasoning.isEmpty()
                || !delta.content.isEmpty() || !delta.toolCalls.isEmpty();
            const bool meaningful = has_output || !delta.finishReason.isEmpty();
            if (meaningful && first_event_ms < 0) first_event_ms = timer.elapsed();
            if (has_output) {
                saw_model_output = true;
                timeout.start(idle_timeout_ms);
            }
            if (!delta.reasoning.isEmpty()) sink.onReasoningDelta(delta.reasoning);
            if (!delta.content.isEmpty()) sink.onContentDelta(delta.content);
            if (!delta.toolCalls.isEmpty()) sink.onToolCallsUpdated(delta.toolCalls);
        }
    };
    const auto consume_chunk = [&decoder, &turn, &raw, &timer, &first_byte_ms,
                                &publish_deltas, reply](const QByteArray &chunk) {
        if (chunk.isEmpty()) return;
        if (first_byte_ms < 0) first_byte_ms = timer.elapsed();
        raw += chunk;
        decoder.feed(chunk);
        publish_deltas();
        if (!decoder.error().isEmpty()) {
            turn.error = decoder.error();
            reply->abort();
        }
    };
    const auto apply_timing = [&turn, &first_byte_ms, &first_event_ms]() {
        turn.timing.firstByteMs = first_byte_ms;
        turn.timing.firstEventMs = first_event_ms;
    };
    QEventLoop loop;
    QObject::connect(reply, &QNetworkReply::readyRead, reply, [reply, &sink, &consume_chunk]() {
        if (sink.isCancelled()) {
            reply->abort();
            return;
        }
        consume_chunk(reply->readAll());
    });
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);

    QObject::connect(&timeout, &QTimer::timeout, reply,
                     [reply, &timed_out, &timed_out_before_output, &saw_model_output]() {
        timed_out = true;
        timed_out_before_output = !saw_model_output;
        reply->abort();
    });
    timeout.start(first_token_timeout_ms);

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
        apply_timing();
        recordTrace(makeHttpTrace(m_config.baseUrl, outgoing.model, payload, raw,
                                  reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt(),
                                  timer.elapsed(), QStringLiteral("cancelled"), m_config.apiKey,
                                  turn.timing));
        reply->deleteLater();
        return turn;
    }
    if (timed_out) {
        turn.error = timed_out_before_output
            ? QStringLiteral("First model output timed out after %1 ms").arg(first_token_timeout_ms)
            : QStringLiteral("Model stream stalled for %1 ms").arg(idle_timeout_ms);
        apply_timing();
        recordTrace(makeHttpTrace(m_config.baseUrl, outgoing.model, payload, raw,
                                  reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt(),
                                  timer.elapsed(), turn.error, m_config.apiKey, turn.timing));
        reply->deleteLater();
        return turn;
    }

    const QByteArray leftover = reply->readAll();
    if (!leftover.isEmpty()) {
        consume_chunk(leftover);
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
        publish_deltas();
    }
    apply_timing();
    turn.error = redactConfiguredSecret(turn.error, m_config.apiKey);
    recordTrace(makeHttpTrace(m_config.baseUrl, outgoing.model, payload, raw,
                              status, timer.elapsed(), turn.error, m_config.apiKey,
                              turn.timing));
    reply->deleteLater();
    return turn;
}

} // namespace SigilAgent
