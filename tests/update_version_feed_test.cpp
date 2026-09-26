#include "Misc/UpdateVersionFeed.h"

#include <QCoreApplication>
#include <QTcpServer>
#include <QTcpSocket>

#include <cstdio>

namespace {

bool Check(const char* label, const QString& actual, const QString& expected)
{
    if (actual == expected) {
        return true;
    }
    std::fprintf(stderr, "%s: expected '%s', got '%s'\n", label,
                 expected.toUtf8().constData(), actual.toUtf8().constData());
    return false;
}

bool CheckHttpFetch()
{
    QTcpServer server;
    if (!server.listen(QHostAddress::LocalHost)) {
        std::fprintf(stderr, "local HTTP server could not listen\n");
        return false;
    }
    QObject::connect(&server, &QTcpServer::newConnection, &server, [&server]() {
        QTcpSocket* socket = server.nextPendingConnection();
        QObject::connect(socket, &QTcpSocket::readyRead, socket, [socket]() {
            socket->readAll();
            const QByteArray body = "<current-version> 2.4.1 </current-version>";
            socket->write("HTTP/1.1 200 OK\r\nContent-Length: "
                          + QByteArray::number(body.size()) + "\r\n\r\n" + body);
            socket->disconnectFromHost();
        });
    });

    const QUrl url(QStringLiteral("http://127.0.0.1:%1/version.xml").arg(server.serverPort()));
    return Check("local HTTP fetch", UpdateVersionFeed::FetchVersion(url, 1000),
                 QStringLiteral("2.4.1"));
}

bool CheckTimeout()
{
    QTcpServer server;
    if (!server.listen(QHostAddress::LocalHost)) {
        std::fprintf(stderr, "timeout server could not listen\n");
        return false;
    }
    // Accept the connection but never send headers.
    QObject::connect(&server, &QTcpServer::newConnection, &server, [&server]() {
        server.nextPendingConnection();
    });

    const QUrl url(QStringLiteral("http://127.0.0.1:%1/version.xml").arg(server.serverPort()));
    return Check("timeout", UpdateVersionFeed::FetchVersion(url, 50), QString());
}

}

int main(int argc, char* argv[])
{
    QCoreApplication app(argc, argv);
    bool ok = true;
    ok &= Check("first match", UpdateVersionFeed::ParseVersion(
                    "<current-version> 1.2.3 </current-version><current-version>9</current-version>"),
                QStringLiteral("1.2.3"));
    ok &= Check("no tag", UpdateVersionFeed::ParseVersion("<version>1.2.3</version>"), QString());
    ok &= Check("Python whitespace", UpdateVersionFeed::ParseVersion(
                    QByteArray("<current-version>") + char(0x1c) + "1.2.3" + char(0x1f)
                    + "</current-version>"), QStringLiteral("1.2.3"));
    ok &= Check("nested tag", UpdateVersionFeed::ParseVersion(
                    "<current-version><b>1.2.3</b></current-version>"), QString());
    ok &= Check("invalid UTF-8", UpdateVersionFeed::ParseVersion(
                    QByteArray::fromHex("ff3c63757272656e742d76657273696f6e3e313c2f63757272656e742d76657273696f6e3e")), QString());
    ok &= Check("truncated UTF-8", UpdateVersionFeed::ParseVersion(
                    QByteArray("<current-version>1.2.3</current-version>\xc2")), QString());
    ok &= CheckHttpFetch();
    ok &= CheckTimeout();
    return ok ? 0 : 1;
}
