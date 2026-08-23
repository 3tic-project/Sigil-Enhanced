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
    return EXIT_SUCCESS;
}
