#include <cstdlib>
#include <iostream>

#include <QJsonArray>
#include <QJsonDocument>
#include <QtEndian>

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

QByteArray RawFrame(const QByteArray &payload)
{
    QByteArray frame(sizeof(quint32), Qt::Uninitialized);
    qToBigEndian(static_cast<quint32>(payload.size()), frame.data());
    frame.append(payload);
    return frame;
}

}

int main()
{
    const QJsonObject request {
        { QStringLiteral("jsonrpc"), QStringLiteral("2.0") },
        { QStringLiteral("id"), 7 },
        { QStringLiteral("method"), QStringLiteral("session.ping") }
    };
    const QByteArray frame = PluginApi::EncodeFrame(request);

    PluginApi::FrameDecoder decoder;
    QList<QJsonObject> messages;
    QString error;
    Require(decoder.Append(frame.left(2), &messages, &error), "partial header failed");
    Require(messages.isEmpty(), "partial header produced a message");
    Require(decoder.Append(frame.mid(2, 5), &messages, &error), "partial payload failed");
    Require(messages.isEmpty(), "partial payload produced a message");
    Require(decoder.Append(frame.mid(7), &messages, &error), "complete frame failed");
    Require(messages.size() == 1 && messages.first() == request, "decoded request differs");
    Require(decoder.BufferedBytes() == 0, "complete frame left buffered bytes");

    messages.clear();
    const QJsonObject second {{ QStringLiteral("event"), QStringLiteral("changed") }};
    Require(decoder.Append(frame + PluginApi::EncodeFrame(second), &messages, &error),
            "coalesced frames failed");
    Require(messages == QList<QJsonObject> { request, second }, "coalesced frame ordering differs");

    messages.clear();
    PluginApi::FrameDecoder tiny_decoder(4);
    Require(!tiny_decoder.Append(frame, &messages, &error), "oversized frame was accepted");
    Require(error.contains(QStringLiteral("limit")), "oversized frame error is unclear");
    Require(tiny_decoder.BufferedBytes() == 0, "oversized frame retained input");

    error.clear();
    Require(!decoder.Append(RawFrame(QByteArrayLiteral("not-json")), &messages, &error),
            "invalid JSON was accepted");
    Require(!error.isEmpty(), "invalid JSON did not report an error");

    decoder.Reset();
    error.clear();
    Require(!decoder.Append(RawFrame(QByteArrayLiteral("[]")), &messages, &error),
            "JSON array was accepted");
    Require(error.contains(QStringLiteral("object")), "JSON object error is unclear");

    const QJsonObject result = PluginApi::MakeResult(7, QJsonObject {{ QStringLiteral("pong"), true }});
    Require(result.value(QStringLiteral("jsonrpc")) == QStringLiteral("2.0"), "result version missing");
    Require(result.value(QStringLiteral("id")).toInt() == 7, "result id differs");
    Require(result.value(QStringLiteral("result")).toObject().value(QStringLiteral("pong")).toBool(),
            "result payload differs");

    const QJsonObject rpc_error = PluginApi::MakeError(
        9, PluginApi::RevisionConflict, QStringLiteral("Revision conflict"),
        QJsonObject {{ QStringLiteral("expected"), 1 }, { QStringLiteral("actual"), 2 }});
    Require(rpc_error.value(QStringLiteral("error")).toObject().value(QStringLiteral("code")).toInt()
                == PluginApi::RevisionConflict,
            "error code differs");

    PluginApi::FrameDecoder capped_decoder(PluginApi::ABSOLUTE_MAX_MESSAGE_SIZE + 1U);
    Require(capped_decoder.MaxMessageSize() == PluginApi::ABSOLUTE_MAX_MESSAGE_SIZE,
            "absolute message limit was not enforced");
    return EXIT_SUCCESS;
}
