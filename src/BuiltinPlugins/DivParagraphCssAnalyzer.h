/************************************************************************
**
**  Copyright (C) 2026 3TIC-Project
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
#ifndef DIVPARAGRAPHCSSANALYZER_H
#define DIVPARAGRAPHCSSANALYZER_H

#include <QString>
#include <QVector>

namespace BuiltinPlugins
{

class DivParagraphCssAnalyzer
{
public:
    struct Source {
        QString id;
        QString text;
        bool available = true;
    };

    struct Dependency {
        QString sourceId;
        QString selector;
        QString reason;
    };

    struct Result {
        bool reviewRequired = false;
        QVector<Dependency> dependencies;
    };

    static Result analyze(const QVector<Source>& sources);
};

}

#endif
