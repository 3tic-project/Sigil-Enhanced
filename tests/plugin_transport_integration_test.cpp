#include <cstdlib>
#include <iostream>

#include <QCoreApplication>
#include <QDir>
#include <QLocalServer>
#include <QLocalSocket>
#include <QProcess>
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

}

int main(int argc, char **argv)
{
    QCoreApplication application(argc, argv);
#ifdef Q_OS_WIN
    const QString server_name = QStringLiteral("sigil-transport-test-")
        + QUuid::createUuid().toString(QUuid::WithoutBraces);
#else
    const QString server_name = QDir::temp().absoluteFilePath(
        QStringLiteral("sigil-transport-test-")
            + QUuid::createUuid().toString(QUuid::WithoutBraces).left(12));
#endif
    QLocalServer::removeServer(server_name);
    QLocalServer server;
    server.setSocketOptions(QLocalServer::UserAccessOption);
    Require(server.listen(server_name), "could not listen on test plugin socket");

    QProcess python;
    python.start(QStringLiteral(SIGIL_TEST_PYTHON), QStringList {
        QStringLiteral(SIGIL_TEST_SOURCE_DIR) + QStringLiteral("/tests/python_live_transport_client.py"),
        server_name,
        QStringLiteral(SIGIL_TEST_SOURCE_DIR)
            + QStringLiteral("/src/Resource_Files/plugin_launchers/python")
    });
    Require(python.waitForStarted(5000), "could not start Python transport client");
    Require(server.waitForNewConnection(5000), "Python transport client did not connect");
    QLocalSocket *socket = server.nextPendingConnection();
    Require(socket != nullptr, "test server has no pending connection");

    const QJsonObject request {
        { QStringLiteral("jsonrpc"), QStringLiteral("2.0") },
        { QStringLiteral("id"), 1 },
        { QStringLiteral("method"), QStringLiteral("session.ping") },
        { QStringLiteral("params"), QJsonObject {{ QStringLiteral("text"), QStringLiteral("line 1\nline 2") }} }
    };
    socket->write(PluginApi::EncodeFrame(request));
    Require(socket->waitForBytesWritten(5000), "test request was not written");
    PluginApi::FrameDecoder decoder;
    QList<QJsonObject> messages;
    QString error;
    while (messages.isEmpty()) {
        Require(socket->bytesAvailable() > 0 || socket->waitForReadyRead(5000),
                "Python transport client did not respond");
        Require(decoder.Append(socket->readAll(), &messages, &error), "Python response frame is invalid");
    }
    Require(messages.size() == 1, "Python response count differs");
    Require(messages.first().value(QStringLiteral("result")).toObject()
                .value(QStringLiteral("pong")).toBool(),
            "Python response payload differs");
    Require(python.waitForFinished(5000), "Python transport client did not exit");
    if (python.exitCode() != 0) {
        std::cerr << python.readAllStandardError().constData() << '\n';
        return EXIT_FAILURE;
    }
    socket->deleteLater();
    server.close();
    QLocalServer::removeServer(server_name);
    return EXIT_SUCCESS;
}
