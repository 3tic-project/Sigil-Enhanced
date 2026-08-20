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
#ifndef KFXIMPORTCONTROLLER_H
#define KFXIMPORTCONTROLLER_H

#include <QCoreApplication>
#include <QJsonObject>
#include <QStringList>

class QWidget;

namespace BuiltinPlugins
{

class KfxImportController
{
public:
    Q_DECLARE_TR_FUNCTIONS(KfxImportController)

public:
    struct Result {
        bool succeeded = false;
        bool cancelled = false;
        QString outputPath;
        QString errorCode;
        QString errorMessage;
        QString diagnosticDetails;
        QStringList warnings;
        QJsonObject summary;
    };

    static Result convert(const QString& sourcePath, QWidget* parent);
    static QString userFacingError(const QString& code, const QString& fallback);

private:
    static QString pythonInterpreter();
};

}

#endif
