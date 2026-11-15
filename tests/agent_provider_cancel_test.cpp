#include <cstdlib>
#include <iostream>

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QHostAddress>
#include <QList>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>

#include "Agent/Core/AgentCancellation.h"
#include "Agent/Model/OpenAICompatibleProvider.h"

namespace
{

void Require(bool condition, const char *message)
{
    if (!condition) {
        std::cerr << message << '\n';
        std::exit(EXIT_FAILURE);
    }
}

class CancelSink : public SigilAgent::ModelStreamSink
{
public:
    explicit CancelSink(SigilAgent::AgentCancellation *cancellation) :
        m_cancellation(cancellation)
    {
    }

    void onReasoningDelta(const QString &) override {}
    void onContentDelta(const QString &) override {}
    void onToolCallsUpdated(const QList<SigilAgent::ToolCall> &) override {}
    bool isCancelled() const override
    {
        return m_cancellation && m_cancellation->isCancelled();
    }

private:
    SigilAgent::AgentCancellation *m_cancellation = nullptr;
};

} // namespace

int main(int argc, char *argv[])
{
    QCoreApplication application(argc, argv);

    QTcpServer server;
    Require(server.listen(QHostAddress::LocalHost, 0), "local socket must accept connections");
    QList<QTcpSocket *> held;
    QObject::connect(&server, &QTcpServer::newConnection, [&server, &held]() {
        while (server.hasPendingConnections()) {
            held.append(server.nextPendingConnection());
        }
    });

    SigilAgent::OpenAIProviderConfig config;
    config.baseUrl = QStringLiteral("http://127.0.0.1:%1/chat/completions").arg(server.serverPort());
    config.apiKey = QStringLiteral("sk-test-not-a-secret");
    config.model = QStringLiteral("mock");
    SigilAgent::OpenAICompatibleProvider provider(config);

    SigilAgent::AgentCancellation cancellation;
    CancelSink sink(&cancellation);

    SigilAgent::ModelRequest request;
    request.model = QStringLiteral("mock");
    request.thinking = true;
    request.stream = true;
    SigilAgent::ChatMessage user;
    user.role = QStringLiteral("user");
    user.content = QStringLiteral("hello");
    request.messages.append(user);

    QElapsedTimer timer;
    timer.start();
    QTimer::singleShot(150, [&cancellation]() { cancellation.request(); });
    const SigilAgent::ModelTurn turn = provider.stream(request, sink);
    const qint64 elapsed_ms = timer.elapsed();

    Require(elapsed_ms < 4000,
            "Stop during an in-flight HTTP wait must abort instead of blocking until timeout");
    Require(turn.error == QStringLiteral("cancelled")
                || turn.finishReason == QStringLiteral("cancelled"),
            "cancelled stream must report cancelled rather than a hang or HTTP timeout");
    cancellation.reset();
    auto responseTest = [&](const QByteArray &response, bool drop) {
        QTcpServer endpoint;
        Require(endpoint.listen(QHostAddress::LocalHost, 0), "fault fixture endpoint");
        QObject::connect(&endpoint, &QTcpServer::newConnection, &endpoint, [&]() {
            while (endpoint.hasPendingConnections()) {
                auto *socket = endpoint.nextPendingConnection();
                QObject::connect(socket, &QTcpSocket::readyRead, socket, [socket, response, drop]() {
                    socket->readAll();
                    if (socket->property("responded").toBool()) return;
                    socket->setProperty("responded", true);
                    if (!response.isEmpty()) socket->write(response);
                    if (drop) socket->disconnectFromHost();
                });
            }
        });
        config.baseUrl = QStringLiteral("http://127.0.0.1:%1/chat/completions").arg(endpoint.serverPort());
        config.firstTokenTimeoutMs = 100;
        SigilAgent::OpenAICompatibleProvider testProvider(config);
        return testProvider.stream(request, sink);
    };
    const auto dropped = responseTest({}, true);
    Require(dropped.retryableError && !dropped.partialOutput, "native transport must classify closed connection without localized string matching");
    const QByteArray limitBody = "{\"error\":{\"message\":\"rate limited\"}}";
    const auto limited = responseTest("HTTP/1.1 429 Too Many Requests\r\nRetry-After: 2\r\nContent-Type: application/json\r\nContent-Length: "
        + QByteArray::number(limitBody.size()) + "\r\n\r\n" + limitBody, true);
    Require(limited.retryableError && limited.retryAfterMs == 2000, "native rate limit must retain Retry-After for the scheduler");
    const auto denied = responseTest("HTTP/1.1 401 Unauthorized\r\nContent-Type: application/json\r\nContent-Length: "
        + QByteArray::number(limitBody.size()) + "\r\n\r\n" + limitBody, true);
    Require(!denied.retryableError, "authentication failure must not become a network retry");
    const auto stalled = responseTest({}, false);
    Require(stalled.retryableError && !stalled.partialOutput, "first-output timeout is retryable");
    const auto partial = responseTest("HTTP/1.1 200 OK\r\nContent-Type: text/event-stream\r\nContent-Length: 4096\r\n\r\n"
        "data: {\"choices\":[{\"delta\":{\"content\":\"already streamed\"}}]}\n\n", true);
    if (!partial.retryableError || !partial.partialOutput) std::cerr << "partial: " << partial.error.toStdString() << " retry=" << partial.retryableError << " output=" << partial.partialOutput << "\n";
    Require(partial.retryableError && partial.partialOutput, "failed stream must remember output even if no completed ModelTurn was decoded");
    return EXIT_SUCCESS;
}
