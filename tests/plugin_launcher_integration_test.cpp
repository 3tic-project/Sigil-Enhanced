#include <cstdlib>
#include <iostream>

#include <QCoreApplication>
#include <QDir>
#include <QJsonArray>
#include <QLocalServer>
#include <QLocalSocket>
#include <QProcess>
#include <QProcessEnvironment>
#include <QUuid>

#include "PluginAPI/PluginProtocol.h"

namespace
{

void Require(bool condition, const char *message)
{
    if (!condition) {
        std::cerr << message << '\n';
        std::exit(EXIT_FAILURE);
    }
}

QJsonObject Receive(QLocalSocket *socket, PluginApi::FrameDecoder *decoder)
{
    QList<QJsonObject> messages;
    QString error;
    while (messages.isEmpty()) {
        Require(socket->bytesAvailable() > 0 || socket->waitForReadyRead(5000),
                "launcher did not send an RPC request");
        Require(decoder->Append(socket->readAll(), &messages, &error), "launcher RPC frame is invalid");
    }
    Require(messages.size() == 1, "launcher sent unexpected coalesced requests");
    return messages.first();
}

void SendResult(QLocalSocket *socket, const QJsonValue &id, const QJsonValue &result)
{
    socket->write(PluginApi::EncodeFrame(PluginApi::MakeResult(id, result)));
    Require(socket->waitForBytesWritten(5000), "host test response was not written");
}

}

int main(int argc, char **argv)
{
    QCoreApplication application(argc, argv);
    const QString token = QStringLiteral("one-time-test-token");
#ifdef Q_OS_WIN
    const QString server_name = QStringLiteral("sigil-launcher-test-")
        + QUuid::createUuid().toString(QUuid::WithoutBraces);
#else
    const QString server_name = QDir::temp().absoluteFilePath(
        QStringLiteral("sigil-launcher-test-")
            + QUuid::createUuid().toString(QUuid::WithoutBraces).left(12));
#endif
    QLocalServer::removeServer(server_name);
    QLocalServer server;
    server.setSocketOptions(QLocalServer::UserAccessOption);
    Require(server.listen(server_name), "could not listen on launcher test socket");

    QProcess process;
    QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
    environment.insert(QStringLiteral("SIGIL_PLUGIN_SOCKET"), server_name);
    environment.insert(QStringLiteral("SIGIL_PLUGIN_TOKEN"), token);
    environment.insert(QStringLiteral("SIGIL_PLUGIN_API_VERSION"), QStringLiteral("2"));
    process.setProcessEnvironment(environment);
    process.start(QStringLiteral(SIGIL_TEST_PYTHON), QStringList {
        QStringLiteral(SIGIL_TEST_SOURCE_DIR)
            + QStringLiteral("/src/Resource_Files/plugin_launchers/python/live_launcher.py"),
        QStringLiteral("--plugin"),
        QStringLiteral(SIGIL_TEST_SOURCE_DIR) + QStringLiteral("/tests/fixtures/live_plugin/plugin.py"),
        QStringLiteral("--plugin-name"), QStringLiteral("LiveFixture")
    });
    Require(process.waitForStarted(5000), "could not start live launcher");
    Require(server.waitForNewConnection(5000), "live launcher did not connect");
    QLocalSocket *socket = server.nextPendingConnection();
    Require(socket != nullptr, "launcher test has no socket");
    PluginApi::FrameDecoder decoder;

    QJsonObject request = Receive(socket, &decoder);
    Require(request.value(QStringLiteral("method")) == QStringLiteral("session.hello"),
            "first launcher request is not session.hello");
    const QJsonObject hello = request.value(QStringLiteral("params")).toObject();
    Require(hello.value(QStringLiteral("token")) == token, "launcher token differs");
    Require(hello.value(QStringLiteral("plugin_name")) == QStringLiteral("LiveFixture"),
            "launcher plugin name differs");
    SendResult(socket, request.value(QStringLiteral("id")), QJsonObject {
        { QStringLiteral("session_id"), QUuid::createUuid().toString(QUuid::WithoutBraces) },
        { QStringLiteral("protocol_version"), 1 },
        { QStringLiteral("api_version"), 2 },
        { QStringLiteral("position_encoding"), QStringLiteral("utf-16") },
        { QStringLiteral("max_message_size"), static_cast<qint64>(PluginApi::DEFAULT_MAX_MESSAGE_SIZE) },
        { QStringLiteral("permissions"), QJsonArray { QStringLiteral("book.read") } }
    });

    request = Receive(socket, &decoder);
    Require(request.value(QStringLiteral("method")) == QStringLiteral("session.ping"),
            "fixture did not call session.ping");
    SendResult(socket, request.value(QStringLiteral("id")), QJsonObject {{ QStringLiteral("pong"), true }});

    request = Receive(socket, &decoder);
    Require(request.value(QStringLiteral("method")) == QStringLiteral("session.finish"),
            "launcher did not finish the session");
    Require(request.value(QStringLiteral("params")).toObject().value(QStringLiteral("status"))
                == QStringLiteral("success"),
            "launcher reported an unexpected status");
    SendResult(socket, request.value(QStringLiteral("id")), QJsonObject {{ QStringLiteral("accepted"), true }});

    Require(process.waitForFinished(5000), "live launcher did not exit");
    if (process.exitCode() != 0) {
        std::cerr << process.readAllStandardError().constData() << '\n';
        return EXIT_FAILURE;
    }
    socket->deleteLater();
    server.close();
    QLocalServer::removeServer(server_name);
    return EXIT_SUCCESS;
}
