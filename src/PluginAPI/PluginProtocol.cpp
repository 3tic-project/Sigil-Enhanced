/************************************************************************
**
**  This file is part of Sigil.
**
**  Sigil is free software: you can redistribute it and/or modify
**  it under the terms of the GNU General Public License as published by
**  the Free Software Foundation, either version 3 of the License, or
**  (at your option) any later version.
**
*************************************************************************/

#include "PluginAPI/PluginProtocol.h"

#include <QtEndian>
#include <QJsonDocument>
#include <QJsonParseError>

namespace PluginApi
{

QByteArray EncodeFrame(const QJsonObject &message)
{
    const QByteArray payload = QJsonDocument(message).toJson(QJsonDocument::Compact);
    QByteArray frame(sizeof(quint32), Qt::Uninitialized);
    qToBigEndian(static_cast<quint32>(payload.size()), frame.data());
    frame.append(payload);
    return frame;
}

QJsonObject MakeResult(const QJsonValue &id, const QJsonValue &result)
{
    return QJsonObject {
        { QStringLiteral("jsonrpc"), QStringLiteral("2.0") },
        { QStringLiteral("id"), id },
        { QStringLiteral("result"), result }
    };
}

QJsonObject MakeError(const QJsonValue &id,
                      int code,
                      const QString &message,
                      const QJsonValue &data)
{
    QJsonObject error {
        { QStringLiteral("code"), code },
        { QStringLiteral("message"), message }
    };
    if (!data.isUndefined()) {
        error.insert(QStringLiteral("data"), data);
    }
    return QJsonObject {
        { QStringLiteral("jsonrpc"), QStringLiteral("2.0") },
        { QStringLiteral("id"), id },
        { QStringLiteral("error"), error }
    };
}

FrameDecoder::FrameDecoder(quint32 max_message_size) :
    m_MaxMessageSize(qMin(max_message_size, ABSOLUTE_MAX_MESSAGE_SIZE))
{
    if (m_MaxMessageSize == 0) {
        m_MaxMessageSize = DEFAULT_MAX_MESSAGE_SIZE;
    }
}

bool FrameDecoder::Append(const QByteArray &bytes,
                          QList<QJsonObject> *messages,
                          QString *error)
{
    if (!messages) {
        if (error) {
            *error = QStringLiteral("Message output is required");
        }
        return false;
    }

    m_Buffer.append(bytes);
    while (m_Buffer.size() >= static_cast<qsizetype>(sizeof(quint32))) {
        const quint32 payload_size = qFromBigEndian<quint32>(m_Buffer.constData());
        if (payload_size == 0) {
            if (error) {
                *error = QStringLiteral("Empty plugin message");
            }
            m_Buffer.clear();
            return false;
        }
        if (payload_size > m_MaxMessageSize) {
            if (error) {
                *error = QStringLiteral("Plugin message exceeds the configured limit");
            }
            m_Buffer.clear();
            return false;
        }

        const qsizetype frame_size = sizeof(quint32) + static_cast<qsizetype>(payload_size);
        if (m_Buffer.size() < frame_size) {
            return true;
        }

        QJsonParseError parse_error;
        const QByteArray payload = m_Buffer.mid(sizeof(quint32), payload_size);
        const QJsonDocument document = QJsonDocument::fromJson(payload, &parse_error);
        if (parse_error.error != QJsonParseError::NoError || !document.isObject()) {
            if (error) {
                *error = parse_error.error == QJsonParseError::NoError
                    ? QStringLiteral("Plugin message must be a JSON object")
                    : parse_error.errorString();
            }
            m_Buffer.clear();
            return false;
        }

        messages->append(document.object());
        m_Buffer.remove(0, frame_size);
    }
    return true;
}

void FrameDecoder::Reset()
{
    m_Buffer.clear();
}

qsizetype FrameDecoder::BufferedBytes() const
{
    return m_Buffer.size();
}

quint32 FrameDecoder::MaxMessageSize() const
{
    return m_MaxMessageSize;
}

} // namespace PluginApi
