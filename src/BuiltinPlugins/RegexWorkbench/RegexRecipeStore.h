/************************************************************************
**
**  Copyright (C) 2026 Sigil-Ebook contributors
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
#ifndef REGEX_RECIPE_STORE_H
#define REGEX_RECIPE_STORE_H

#include <QByteArray>
#include <QList>
#include <QString>

#include "BuiltinPlugins/RegexWorkbench/RegexWorkbenchTypes.h"
#include "BuiltinPlugins/RegexWorkbench/SearchVariableStore.h"

namespace BuiltinPlugins
{
namespace RegexWorkbench
{

struct RegexRecipe
{
    QString name;
    VariableScope variableScope = VariableScope::Batch;
    WritePolicy writePolicy = WritePolicy::LastWins;
    QList<RegexWorkbenchRule> rules;
};

struct RegexRecipeLimits
{
    qint64 maxFileBytes = 4 * 1024 * 1024;
    int maxRules = 1000;
    int maxIdCodeUnits = 128;
    int maxNameCodeUnits = 1024;
    int maxPatternCodeUnits = 1024 * 1024;
    int maxCaptureNamesPerRule = 256;
    int maxIterations = 10000;
};

class RegexRecipeStore final
{
public:
    static constexpr int CurrentVersion = 1;

    static QByteArray Serialize(
        const RegexRecipe& recipe,
        QString* error = nullptr,
        RegexRecipeLimits limits = RegexRecipeLimits());
    static bool Deserialize(
        const QByteArray& data,
        RegexRecipe& recipe,
        QString* error = nullptr,
        RegexRecipeLimits limits = RegexRecipeLimits());

    static bool SaveFile(
        const QString& path,
        const RegexRecipe& recipe,
        QString* error = nullptr,
        RegexRecipeLimits limits = RegexRecipeLimits());
    static bool LoadFile(
        const QString& path,
        RegexRecipe& recipe,
        QString* error = nullptr,
        RegexRecipeLimits limits = RegexRecipeLimits());
    static bool LoadNamed(
        const QString& identifier,
        RegexRecipe& recipe,
        QString* resolvedPath = nullptr,
        QString* error = nullptr,
        RegexRecipeLimits limits = RegexRecipeLimits());

    static QString DefaultDirectory();
    static bool Validate(
        const RegexRecipe& recipe,
        QString* error = nullptr,
        RegexRecipeLimits limits = RegexRecipeLimits());
};

}
}

#endif // REGEX_RECIPE_STORE_H
