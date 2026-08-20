/************************************************************************
**
**  Copyright (C) 2026 Sigil-Enhanced contributors
**
**  This file is part of Sigil-Enhanced.
**
**  Sigil-Enhanced is free software: you can redistribute it and/or modify
**  it under the terms of the GNU General Public License as published by
**  the Free Software Foundation, either version 3 of the License, or
**  (at your option) any later version.
**
*************************************************************************/

#include "BuiltinPlugins/KfxImportProtocol.h"

#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonParseError>

namespace BuiltinPlugins
{

bool KfxImportProtocol::isKfxPath(const QString& path)
{
    const QString name = QFileInfo(path).fileName();
    return name.endsWith(QStringLiteral(".kfx"), Qt::CaseInsensitive)
        || name.endsWith(QStringLiteral(".kfx-zip"), Qt::CaseInsensitive);
}

QString KfxImportProtocol::suggestedEpubName(const QString& path)
{
    QString name = QFileInfo(path).fileName();
    if (name.endsWith(QStringLiteral(".kfx-zip"), Qt::CaseInsensitive)) {
        name.chop(8);
    } else if (name.endsWith(QStringLiteral(".kfx"), Qt::CaseInsensitive)) {
        name.chop(4);
    }
    if (name.trimmed().isEmpty()) {
        name = QStringLiteral("converted");
    }
    return name + QStringLiteral(".epub");
}

bool KfxImportProtocol::parseLine(const QByteArray& line, KfxWorkerEvent* event, QString* error)
{
    if (!event) {
        if (error) *error = QStringLiteral("No event destination was supplied.");
        return false;
    }
    *event = KfxWorkerEvent();

    QJsonParseError parse_error;
    const QJsonDocument document = QJsonDocument::fromJson(line.trimmed(), &parse_error);
    if (parse_error.error != QJsonParseError::NoError || !document.isObject()) {
        if (error) *error = QStringLiteral("Invalid JSON event: %1").arg(parse_error.errorString());
        return false;
    }

    const QJsonObject object = document.object();
    if (object.value(QStringLiteral("protocol")).toInt(-1) != Version) {
        if (error) *error = QStringLiteral("Unsupported KFX worker protocol version.");
        return false;
    }

    const QString event_name = object.value(QStringLiteral("event")).toString();
    if (event_name == QStringLiteral("started")) {
        event->type = KfxWorkerEvent::Started;
    } else if (event_name == QStringLiteral("phase")) {
        event->type = KfxWorkerEvent::Phase;
        event->name = object.value(QStringLiteral("name")).toString();
    } else if (event_name == QStringLiteral("progress")) {
        event->type = KfxWorkerEvent::Progress;
        event->name = object.value(QStringLiteral("phase")).toString();
        event->current = object.value(QStringLiteral("current")).toInt();
        event->total = object.value(QStringLiteral("total")).toInt();
    } else if (event_name == QStringLiteral("warning")) {
        event->type = KfxWorkerEvent::Warning;
        event->code = object.value(QStringLiteral("code")).toString();
        event->message = object.value(QStringLiteral("message")).toString();
    } else if (event_name == QStringLiteral("result")) {
        const QString status = object.value(QStringLiteral("status")).toString();
        if (status == QStringLiteral("success")) {
            event->type = KfxWorkerEvent::Success;
            event->outputPath = object.value(QStringLiteral("output")).toString();
            event->summary = object.value(QStringLiteral("summary")).toObject();
        } else if (status == QStringLiteral("error")) {
            event->type = KfxWorkerEvent::Error;
            event->code = object.value(QStringLiteral("code")).toString();
            event->message = object.value(QStringLiteral("message")).toString();
        }
    }

    if (event->type == KfxWorkerEvent::Invalid) {
        if (error) *error = QStringLiteral("Unknown or incomplete KFX worker event.");
        return false;
    }
    return true;
}

}
