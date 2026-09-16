/************************************************************************
**
**  Copyright (C) 2026 3TIC-Project
**
**  This file is part of Sigil-Enhanced.
**
*************************************************************************/

#pragma once
#ifndef DIVPARAGRAPHSTYLESHEETRESOLVER_H
#define DIVPARAGRAPHSTYLESHEETRESOLVER_H

#include <QHash>
#include <QString>
#include <QVector>

#include "BuiltinPlugins/DivParagraphCssAnalyzer.h"

namespace BuiltinPlugins
{

class DivParagraphStylesheetResolver
{
public:
    static QVector<DivParagraphCssAnalyzer::Source> resolve(
        const QString& xhtml,
        const QString& xhtmlBookPath,
        const QHash<QString, QString>& cssByBookPath);
};

}

#endif
