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

#pragma once
#ifndef PLUGINPROTOCOL_H
#define PLUGINPROTOCOL_H

#include <QByteArray>
#include <QJsonObject>
#include <QList>
#include <QString>

namespace PluginApi
{

constexpr int PROTOCOL_VERSION = 1;
constexpr int API_VERSION = 2;
constexpr quint32 DEFAULT_MAX_MESSAGE_SIZE = 8U * 1024U * 1024U;
constexpr quint32 ABSOLUTE_MAX_MESSAGE_SIZE = 32U * 1024U * 1024U;

enum ErrorCode {
    PermissionDenied = -32001,
    BookClosed = -32002,
    ResourceNotFound = -32003,
    RevisionConflict = -32004,
    InvalidPatch = -32005,
    TransactionRequired = -32006,
    ValidationFailed = -32007,
    PayloadTooLarge = -32008,
    Busy = -32009,
    UnsupportedOperation = -32010,
    TransactionNotFound = -32011,
    SessionEnding = -32012
};

QByteArray EncodeFrame(const QJsonObject &message);
QJsonObject MakeResult(const QJsonValue &id, const QJsonValue &result);
QJsonObject MakeError(const QJsonValue &id,
                      int code,
                      const QString &message,
                      const QJsonValue &data = QJsonValue());

class FrameDecoder
{
public:
    explicit FrameDecoder(quint32 max_message_size = DEFAULT_MAX_MESSAGE_SIZE);

    bool Append(const QByteArray &bytes, QList<QJsonObject> *messages, QString *error);
    void Reset();
    qsizetype BufferedBytes() const;
    quint32 MaxMessageSize() const;

private:
    QByteArray m_Buffer;
    quint32 m_MaxMessageSize;
};

} // namespace PluginApi

#endif // PLUGINPROTOCOL_H
