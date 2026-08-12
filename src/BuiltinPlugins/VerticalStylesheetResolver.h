/************************************************************************
**
**  Copyright (C) 2026 3TIC-Project
**
**  This file is part of Sigil-Enhanced.
**
*************************************************************************/

#pragma once
#ifndef VERTICALSTYLESHEETRESOLVER_H
#define VERTICALSTYLESHEETRESOLVER_H

#include <QHash>
#include <QString>
#include <QStringList>

namespace BuiltinPlugins
{

class VerticalStylesheetResolver
{
public:
    // Resolve linked stylesheets and their transitive @import graph to book
    // paths without reading the filesystem.
    static QStringList resolve(const QString& xhtmlSource,
                               const QString& xhtmlBookPath,
                               const QHash<QString, QString>& cssByBookPath);
};

}

#endif // VERTICALSTYLESHEETRESOLVER_H
