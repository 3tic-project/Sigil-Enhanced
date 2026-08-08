/************************************************************************
**
**  Copyright (C) 2026  Sigil-Enhanced contributors
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

#include <functional>

#include <QHash>
#include <QList>
#include <QString>
#include <QStringList>

namespace SearchBatch
{

enum class Engine {
    Regex,
    PreSearch,
    PythonFunction
};

struct Rule {
    QString id;
    QString name;
    QString searchRegex;
    QString replacement;
    QString preSearchRegex;
    QString pythonFunctionName;
    QStringList resourcePaths;
    Engine engine = Engine::Regex;
};

struct ApplyResult {
    bool ok = true;
    QString text;
    qint64 replacementCount = 0;
    QString error;
};

struct RuleResult {
    QString id;
    QString name;
    qint64 replacementCount = 0;
    int matchedResourceCount = 0;
    int changedResourceCount = 0;
};

struct Result {
    bool success = false;
    bool cancelled = false;
    qint64 replacementCount = 0;
    QList<RuleResult> rules;
    QHash<QString, QString> changedTexts;
    QString error;
};

using ApplyFunction = std::function<ApplyResult(const Rule&, const QString&, const QString&)>;
using CancelFunction = std::function<bool()>;

class Runner final
{
public:
    static Result Run(const QList<Rule>& rules,
                      const QHash<QString, QString>& originalTexts,
                      const ApplyFunction& apply,
                      const CancelFunction& isCancelled = CancelFunction());
};

}
