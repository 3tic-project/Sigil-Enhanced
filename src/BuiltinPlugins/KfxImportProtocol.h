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

#pragma once
#ifndef KFXIMPORTPROTOCOL_H
#define KFXIMPORTPROTOCOL_H

#include <QByteArray>
#include <QJsonObject>
#include <QString>

namespace BuiltinPlugins
{

struct KfxWorkerEvent
{
    enum Type {
        Invalid,
        Ignored,
        Started,
        Phase,
        Progress,
        Warning,
        Success,
        Error
    };

    Type type = Invalid;
    QString name;
    QString code;
    QString message;
    QString outputPath;
    int current = 0;
    int total = 0;
    QJsonObject summary;
};

class KfxImportProtocol
{
public:
    static constexpr int Version = 1;

    static bool isKfxPath(const QString& path);
    static QString suggestedEpubName(const QString& path);
    static bool parseLine(const QByteArray& line, KfxWorkerEvent* event, QString* error = nullptr);
};

}

#endif
